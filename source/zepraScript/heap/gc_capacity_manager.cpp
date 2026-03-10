// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_capacity_manager.cpp — Dynamic object capacity tracking and growth policy

#include <atomic>
#include <mutex>
#include <functional>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <algorithm>

namespace Zepra::Heap {

class CapacityManager {
public:
    struct Config {
        size_t initialCapacity;
        size_t maxCapacity;
        double growThreshold;     // Grow when usage exceeds this ratio
        double shrinkThreshold;   // Shrink when usage drops below this ratio
        double growFactor;
        double shrinkFactor;

        Config()
            : initialCapacity(80000)
            , maxCapacity(262144)
            , growThreshold(0.75)
            , shrinkThreshold(0.25)
            , growFactor(2.0)
            , shrinkFactor(0.5) {}
    };

    struct Callbacks {
        std::function<bool(size_t newCapacity)> resizeObjectTable;
        std::function<void()> triggerCompaction;
        std::function<size_t()> getLiveObjectCount;
    };

    explicit CapacityManager(const Config& config = Config{})
        : config_(config)
        , currentCapacity_(config.initialCapacity) {}

    void setCallbacks(Callbacks cb) { cb_ = std::move(cb); }

    bool maybeGrow() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t live = liveCount();
        double usage = static_cast<double>(live) / currentCapacity_;

        if (usage < config_.growThreshold) return false;

        size_t target = static_cast<size_t>(currentCapacity_ * config_.growFactor);
        target = std::min(target, config_.maxCapacity);
        if (target <= currentCapacity_) {
            // At max capacity — compact instead of growing.
            if (cb_.triggerCompaction) cb_.triggerCompaction();
            stats_.compactionsTrigered++;
            return false;
        }

        if (cb_.resizeObjectTable && cb_.resizeObjectTable(target)) {
            currentCapacity_ = target;
            stats_.grows++;
            return true;
        }
        return false;
    }

    bool maybeShrink() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t live = liveCount();
        double usage = static_cast<double>(live) / currentCapacity_;

        if (usage > config_.shrinkThreshold) return false;
        if (currentCapacity_ <= config_.initialCapacity) return false;

        size_t target = std::max(
            static_cast<size_t>(currentCapacity_ * config_.shrinkFactor),
            config_.initialCapacity);

        // Never shrink below 2x live count.
        target = std::max(target, live * 2);

        if (target >= currentCapacity_) return false;

        if (cb_.resizeObjectTable && cb_.resizeObjectTable(target)) {
            currentCapacity_ = target;
            stats_.shrinks++;
            return true;
        }
        return false;
    }

    double heapGrowthFactor(size_t liveBytes, size_t heapBytes) const {
        if (heapBytes == 0) return config_.growFactor;
        double liveRatio = static_cast<double>(liveBytes) / heapBytes;
        // High survival ratio → grow aggressively. Low → grow conservatively.
        if (liveRatio > 0.8) return 2.0;
        if (liveRatio > 0.5) return 1.5;
        return 1.2;
    }

    size_t currentCapacity() const { return currentCapacity_; }
    size_t maxCapacity() const { return config_.maxCapacity; }

    size_t liveCount() const {
        return cb_.getLiveObjectCount ? cb_.getLiveObjectCount() : 0;
    }

    double usageRatio() const {
        return currentCapacity_ > 0
            ? static_cast<double>(liveCount()) / currentCapacity_
            : 0;
    }

    bool isAtMaxCapacity() const {
        return currentCapacity_ >= config_.maxCapacity;
    }

    struct Stats {
        uint64_t grows;
        uint64_t shrinks;
        uint64_t compactionsTrigered;
        size_t peakCapacity;
    };

    Stats stats() const {
        Stats s = stats_;
        s.peakCapacity = peakCapacity_;
        return s;
    }

    void recordPeak() {
        if (currentCapacity_ > peakCapacity_) {
            peakCapacity_ = currentCapacity_;
        }
    }

private:
    Config config_;
    Callbacks cb_;
    size_t currentCapacity_;
    size_t peakCapacity_ = 0;
    Stats stats_{};
    mutable std::mutex mutex_;
};

} // namespace Zepra::Heap
