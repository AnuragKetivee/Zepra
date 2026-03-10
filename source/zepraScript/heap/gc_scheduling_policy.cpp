// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_scheduling_policy.cpp — GC trigger heuristics, smoothing, prediction

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cmath>
#include <atomic>
#include <functional>
#include <algorithm>
#include <chrono>

namespace Zepra::Heap {

enum class GCTrigger : uint8_t {
    None,
    AllocationBudget,    // Exceeded allocation budget since last GC
    TimeBased,           // Exceeded time since last GC
    IdleTime,            // CPU idle detected
    MemoryPressure,      // System memory pressure signal
    Proactive,           // Predicted GC need
    Explicit,            // User or embedding API requested GC
};

enum class GCKind : uint8_t {
    Minor,      // Nursery only
    Major,      // Full heap (incremental)
    Full,       // Full non-incremental (emergency)
    Compact,    // Major + compaction
};

struct SchedulingConfig {
    size_t minorGCThresholdBytes;       // Nursery fill trigger
    size_t majorGCThresholdBytes;       // Old-gen allocation trigger
    double majorGCGrowthFactor;         // Trigger at liveBytes * factor
    double timeTriggerMs;               // Max ms between GCs
    double idleDeadlineMs;              // Minimum idle time to start GC
    double smoothingFactor;             // Exponential smoothing for predictions

    SchedulingConfig()
        : minorGCThresholdBytes(256 * 1024)    // 256KB nursery
        , majorGCThresholdBytes(4 * 1024 * 1024)
        , majorGCGrowthFactor(1.7)
        , timeTriggerMs(30000.0)               // 30s max between GCs
        , idleDeadlineMs(10.0)
        , smoothingFactor(0.7) {}
};

class GCSchedulingPolicy {
public:
    explicit GCSchedulingPolicy(const SchedulingConfig& config = SchedulingConfig{})
        : config_(config), lastGCTimestamp_(now()), lastLiveBytes_(0)
        , allocatedSinceGC_(0), predictedLiveBytes_(0), gcCount_(0) {}

    // Check if GC should be triggered. Returns trigger reason.
    GCTrigger shouldCollect(size_t heapBytes, size_t liveBytes, size_t nurseryUsed) {
        // 1. Nursery full → minor GC.
        if (nurseryUsed >= config_.minorGCThresholdBytes) {
            return GCTrigger::AllocationBudget;
        }

        // 2. Old-gen growth → major GC.
        size_t threshold = computeMajorThreshold(lastLiveBytes_);
        if (heapBytes >= threshold) {
            return GCTrigger::AllocationBudget;
        }

        // 3. Time-based trigger.
        double elapsed = elapsedSinceLastGC();
        if (elapsed >= config_.timeTriggerMs) {
            return GCTrigger::TimeBased;
        }

        return GCTrigger::None;
    }

    // Determine what kind of GC to run.
    GCKind selectGCKind(GCTrigger trigger, size_t heapBytes, size_t liveBytes,
                        double fragmentation) {
        switch (trigger) {
            case GCTrigger::AllocationBudget:
                if (heapBytes < config_.majorGCThresholdBytes) return GCKind::Minor;
                if (fragmentation > 0.3) return GCKind::Compact;
                return GCKind::Major;

            case GCTrigger::MemoryPressure:
                return GCKind::Full;

            case GCTrigger::IdleTime:
                return GCKind::Minor;

            case GCTrigger::TimeBased:
                return GCKind::Major;

            case GCTrigger::Proactive:
                return GCKind::Major;

            case GCTrigger::Explicit:
                return GCKind::Full;

            default:
                return GCKind::Minor;
        }
    }

    // Update state after a GC cycle.
    void recordGCComplete(size_t liveBytes, double durationMs) {
        lastGCTimestamp_ = now();
        gcCount_++;

        // Exponential smoothing for live bytes prediction.
        if (predictedLiveBytes_ == 0) {
            predictedLiveBytes_ = liveBytes;
        } else {
            predictedLiveBytes_ = static_cast<size_t>(
                config_.smoothingFactor * liveBytes +
                (1.0 - config_.smoothingFactor) * predictedLiveBytes_);
        }

        // Track GC duration for pause-time management.
        if (avgGCDurationMs_ == 0) {
            avgGCDurationMs_ = durationMs;
        } else {
            avgGCDurationMs_ = config_.smoothingFactor * durationMs +
                              (1.0 - config_.smoothingFactor) * avgGCDurationMs_;
        }

        lastLiveBytes_ = liveBytes;
        allocatedSinceGC_ = 0;
    }

    void recordAllocation(size_t bytes) {
        allocatedSinceGC_ += bytes;
    }

    // Proactive: predict when next GC will be needed.
    size_t predictedBytesUntilGC() const {
        size_t threshold = computeMajorThreshold(predictedLiveBytes_);
        return threshold > allocatedSinceGC_ ? threshold - allocatedSinceGC_ : 0;
    }

    void setConfig(const SchedulingConfig& config) { config_ = config; }
    const SchedulingConfig& config() const { return config_; }

    size_t lastLiveBytes() const { return lastLiveBytes_; }
    size_t predictedLiveBytes() const { return predictedLiveBytes_; }
    double avgGCDurationMs() const { return avgGCDurationMs_; }
    uint64_t gcCount() const { return gcCount_; }

private:
    size_t computeMajorThreshold(size_t liveBytes) const {
        return static_cast<size_t>(
            std::max(static_cast<double>(config_.majorGCThresholdBytes),
                     liveBytes * config_.majorGCGrowthFactor));
    }

    double elapsedSinceLastGC() const {
        return std::chrono::duration<double, std::milli>(now() - lastGCTimestamp_).count();
    }

    static std::chrono::steady_clock::time_point now() {
        return std::chrono::steady_clock::now();
    }

    SchedulingConfig config_;
    std::chrono::steady_clock::time_point lastGCTimestamp_;
    size_t lastLiveBytes_;
    size_t allocatedSinceGC_;
    size_t predictedLiveBytes_;
    double avgGCDurationMs_ = 0;
    uint64_t gcCount_;
};

} // namespace Zepra::Heap
