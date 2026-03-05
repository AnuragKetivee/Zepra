/**
 * @file gc_scheduler_impl.cpp
 * @brief GC scheduling heuristics and trigger policies
 *
 * Decides when to run GC, which type, and how aggressively.
 */

#include <atomic>
#include <mutex>
#include <vector>
#include <deque>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cassert>
#include <memory>
#include <algorithm>
#include <cmath>

namespace Zepra::Heap {

enum class CollectionType : uint8_t {
    None, IncrementalStep, MinorGC, MajorGC, FullGC, CompactGC, EmergencyGC
};

enum class TriggerReason : uint8_t {
    AllocationThreshold, NurseryFull, TimeBased, MemoryPressure,
    IdleNotification, ExternalRequest, AllocationFailure, OOMImminent, Compaction
};

struct SchedulingDecision {
    CollectionType type;
    TriggerReason reason;
    bool shouldCompact;
    bool shouldShrinkHeap;
    bool shouldGrowHeap;
    size_t targetHeapSize;
    double urgency;
};

class AllocationRateTracker {
public:
    struct Sample { uint64_t timestampUs; size_t bytesAllocated; };

    void recordAllocation(size_t bytes) {
        totalBytes_.fetch_add(bytes, std::memory_order_relaxed);
        totalCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void snapshot() {
        uint64_t now = nowUs();
        Sample s; s.timestampUs = now;
        s.bytesAllocated = totalBytes_.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.push_back(s);
        if (samples_.size() > maxSamples_) samples_.pop_front();
    }

    double rateBytePerMs(size_t windowMs = 1000) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.size() < 2) return 0;
        uint64_t windowUs = windowMs * 1000;
        uint64_t now = samples_.back().timestampUs;
        uint64_t cutoff = now > windowUs ? now - windowUs : 0;
        auto it = samples_.begin();
        while (it != samples_.end() && it->timestampUs < cutoff) ++it;
        if (it == samples_.end() || it == std::prev(samples_.end())) return 0;
        size_t bytesDelta = samples_.back().bytesAllocated - it->bytesAllocated;
        uint64_t timeDelta = samples_.back().timestampUs - it->timestampUs;
        if (timeDelta == 0) return 0;
        return static_cast<double>(bytesDelta) / (static_cast<double>(timeDelta) / 1000.0);
    }

    double timeToFullMs(size_t currentUsed, size_t heapLimit) const {
        double rate = rateBytePerMs();
        if (rate <= 0) return 1e9;
        size_t remaining = heapLimit > currentUsed ? heapLimit - currentUsed : 0;
        return static_cast<double>(remaining) / rate;
    }

    size_t totalAllocated() const { return totalBytes_.load(std::memory_order_relaxed); }

private:
    static uint64_t nowUs() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    std::atomic<size_t> totalBytes_{0};
    std::atomic<size_t> totalCount_{0};
    std::deque<Sample> samples_;
    size_t maxSamples_ = 100;
    mutable std::mutex mutex_;
};

class GCSchedulerImpl {
public:
    struct Config {
        size_t minorGCThresholdBytes;
        double majorGCGrowthFactor;
        uint64_t maxGCIntervalMs;
        double moderatePressureRatio;
        double criticalPressureRatio;
        double compactionFragThreshold;
        size_t compactionMinPages;
        size_t minHeapSize;
        size_t maxHeapSize;
        double heapGrowthFactor;
        double heapShrinkFactor;
        double targetMutatorUtilization;
        double maxPauseMs;

        Config()
            : minorGCThresholdBytes(2 * 1024 * 1024)
            , majorGCGrowthFactor(1.5)
            , maxGCIntervalMs(30000)
            , moderatePressureRatio(0.15)
            , criticalPressureRatio(0.05)
            , compactionFragThreshold(0.3)
            , compactionMinPages(4)
            , minHeapSize(4 * 1024 * 1024)
            , maxHeapSize(512 * 1024 * 1024)
            , heapGrowthFactor(2.0)
            , heapShrinkFactor(0.75)
            , targetMutatorUtilization(0.95)
            , maxPauseMs(5.0) {}
    };

    struct HeapSnapshot {
        size_t nurseryUsed;
        size_t nurseryCapacity;
        size_t oldGenUsed;
        size_t oldGenCapacity;
        size_t losUsed;
        size_t heapLimit;
        size_t externalMemory;
        double fragmentation;
        size_t sparsePages;
    };

    explicit GCSchedulerImpl(const Config& config = Config{}) : config_(config) {}

    void updateHeapState(const HeapSnapshot& snapshot) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastSnapshot_ = snapshot;
        allocationTracker_.snapshot();
    }

    SchedulingDecision shouldCollect() const {
        std::lock_guard<std::mutex> lock(mutex_);
        SchedulingDecision d{};
        d.type = CollectionType::None;
        d.shouldCompact = false;
        d.shouldShrinkHeap = false;
        d.shouldGrowHeap = false;
        d.urgency = 0;

        if (shouldTriggerMinor()) {
            d.type = CollectionType::MinorGC;
            d.reason = TriggerReason::NurseryFull;
            d.urgency = 0.5;
            return d;
        }
        if (shouldTriggerMajor()) {
            d.type = CollectionType::MajorGC;
            d.reason = TriggerReason::AllocationThreshold;
            d.urgency = 0.7;
            if (shouldCompactCheck()) {
                d.type = CollectionType::CompactGC;
                d.shouldCompact = true;
            }
            return d;
        }
        uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if (now - lastGCTimestampUs_ > config_.maxGCIntervalMs * 1000) {
            d.type = CollectionType::IncrementalStep;
            d.reason = TriggerReason::TimeBased;
            d.urgency = 0.2;
            return d;
        }
        if (shouldGrowHeap()) { d.shouldGrowHeap = true; d.targetHeapSize = computeTargetHeapSize(lastSnapshot_.oldGenUsed); }
        else if (shouldShrinkHeap()) { d.shouldShrinkHeap = true; d.targetHeapSize = computeTargetHeapSize(lastSnapshot_.oldGenUsed); }
        return d;
    }

    void notifyGCComplete(CollectionType type, double pauseMs, size_t) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastGCTimestampUs_ = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        lastPauseMs_ = pauseMs;
        totalPauseMs_ += pauseMs;
        totalGCCount_++;
        if (type == CollectionType::MajorGC || type == CollectionType::FullGC || type == CollectionType::CompactGC) {
            lastMajorGCTimestampUs_ = lastGCTimestampUs_;
            heapSizeAtLastMajorGC_ = lastSnapshot_.oldGenUsed;
        }
    }

    SchedulingDecision idleNotification(double deadlineMs) const {
        SchedulingDecision d{}; d.type = CollectionType::None; d.urgency = 0;
        if (deadlineMs > 1.0) { d.type = CollectionType::IncrementalStep; d.reason = TriggerReason::IdleNotification; d.urgency = 0.1; }
        return d;
    }

    SchedulingDecision allocationFailure(size_t) const {
        SchedulingDecision d{}; d.type = CollectionType::FullGC; d.reason = TriggerReason::AllocationFailure; d.urgency = 0.9;
        return d;
    }

    SchedulingDecision memoryPressure(int level) const {
        SchedulingDecision d{};
        if (level >= 2) { d.type = CollectionType::EmergencyGC; d.reason = TriggerReason::OOMImminent; d.urgency = 1.0; d.shouldShrinkHeap = true; }
        else if (level >= 1) { d.type = CollectionType::FullGC; d.reason = TriggerReason::MemoryPressure; d.urgency = 0.8; }
        return d;
    }

    AllocationRateTracker& allocationTracker() { return allocationTracker_; }

    size_t computeTargetHeapSize(size_t liveBytes) const {
        size_t target = static_cast<size_t>(static_cast<double>(liveBytes) * config_.heapGrowthFactor);
        target = std::max(target, config_.minHeapSize);
        target = std::min(target, config_.maxHeapSize);
        return target;
    }

private:
    bool shouldTriggerMinor() const { return lastSnapshot_.nurseryUsed >= lastSnapshot_.nurseryCapacity * 0.9; }
    bool shouldTriggerMajor() const {
        if (heapSizeAtLastMajorGC_ == 0) return false;
        return static_cast<double>(lastSnapshot_.oldGenUsed) / static_cast<double>(heapSizeAtLastMajorGC_) >= config_.majorGCGrowthFactor;
    }
    bool shouldCompactCheck() const { return lastSnapshot_.fragmentation >= config_.compactionFragThreshold && lastSnapshot_.sparsePages >= config_.compactionMinPages; }
    bool shouldGrowHeap() const {
        if (lastSnapshot_.heapLimit == 0) return false;
        return static_cast<double>(lastSnapshot_.oldGenUsed) / static_cast<double>(lastSnapshot_.heapLimit) > 0.85 && lastSnapshot_.heapLimit < config_.maxHeapSize;
    }
    bool shouldShrinkHeap() const {
        if (lastSnapshot_.heapLimit == 0) return false;
        return static_cast<double>(lastSnapshot_.oldGenUsed) / static_cast<double>(lastSnapshot_.heapLimit) < 0.3 && lastSnapshot_.heapLimit > config_.minHeapSize;
    }

    Config config_;
    HeapSnapshot lastSnapshot_{};
    AllocationRateTracker allocationTracker_;
    uint64_t lastGCTimestampUs_ = 0;
    uint64_t lastMajorGCTimestampUs_ = 0;
    size_t heapSizeAtLastMajorGC_ = 0;
    double lastPauseMs_ = 0;
    double totalPauseMs_ = 0;
    uint64_t totalGCCount_ = 0;
    mutable std::mutex mutex_;
};

class PromotionRatePredictor {
public:
    void recordScavenge(size_t nurseryUsed, size_t promoted) {
        if (nurseryUsed == 0) return;
        double rate = static_cast<double>(promoted) / static_cast<double>(nurseryUsed);
        if (initialized_) { ewmaRate_ = 0.3 * rate + 0.7 * ewmaRate_; }
        else { ewmaRate_ = rate; initialized_ = true; }
        history_.push_back(rate);
        if (history_.size() > 50) history_.pop_front();
    }

    double predictedRate() const { return ewmaRate_; }

    size_t predictedPromotionBytes(size_t nurseryUsed) const {
        return static_cast<size_t>(static_cast<double>(nurseryUsed) * ewmaRate_);
    }

    bool isTrendingUp() const {
        if (history_.size() < 10) return false;
        size_t half = history_.size() / 2;
        double older = 0, recent = 0;
        for (size_t i = 0; i < half; i++) older += history_[i];
        for (size_t i = half; i < history_.size(); i++) recent += history_[i];
        older /= static_cast<double>(half);
        recent /= static_cast<double>(history_.size() - half);
        return recent > older * 1.1;
    }

private:
    double ewmaRate_ = 0;
    bool initialized_ = false;
    std::deque<double> history_;
};

} // namespace Zepra::Heap
