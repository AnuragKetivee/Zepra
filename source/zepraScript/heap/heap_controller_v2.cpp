// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file heap_controller_v2.cpp
 * @brief Advanced heap sizing and GC triggering controller
 *
 * Determines:
 * 1. When to trigger GC (threshold management)
 * 2. What type of GC to run (minor/major/compact)
 * 3. How to size the heap after GC (grow/shrink)
 *
 * Uses a feedback loop:
 * - Allocation rate (bytes/ms) → predicts when heap will fill
 * - GC efficiency (bytes reclaimed / time spent) → cost model
 * - Memory pressure → external constraints from OS
 * - Mutator utilization → % time not in GC (target: >95%)
 *
 * Heap sizing strategy:
 * - After GC, set new threshold = liveBytes * growthFactor
 * - Growth factor adapts based on GC frequency and efficiency
 * - Shrink heap if consistently low occupancy
 * - Never shrink below a minimum or above OS limit
 *
 * GC type selection:
 * - Minor if nursery full (fast, ~1ms pause)
 * - Major if old gen threshold hit (concurrent, ~5ms STW)
 * - Compact if fragmentation > threshold
 * - Full if allocation failure after major GC (emergency)
 */

#include <atomic>
#include <mutex>
#include <vector>
#include <deque>
#include <functional>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <memory>
#include <algorithm>
#include <cmath>

namespace Zepra::Heap {

// =============================================================================
// Allocation Rate Tracker
// =============================================================================

class AllocationRateTracker {
public:
    void recordAllocation(size_t bytes) {
        totalAllocated_ += bytes;
        sampleCount_++;
    }

    void tick(double elapsedMs) {
        if (elapsedMs <= 0) return;
        double rate = static_cast<double>(totalAllocated_) / elapsedMs;

        // EWMA smoothing
        if (smoothedRate_ < 0) {
            smoothedRate_ = rate;
        } else {
            smoothedRate_ = ALPHA * rate + (1.0 - ALPHA) * smoothedRate_;
        }

        totalAllocated_ = 0;
        sampleCount_ = 0;
    }

    double bytesPerMs() const {
        return smoothedRate_ > 0 ? smoothedRate_ : 0;
    }

    double predictTimeToFull(size_t freeBytes) const {
        if (smoothedRate_ <= 0) return 1e9;
        return static_cast<double>(freeBytes) / smoothedRate_;
    }

private:
    static constexpr double ALPHA = 0.3;
    size_t totalAllocated_ = 0;
    size_t sampleCount_ = 0;
    double smoothedRate_ = -1;
};

// =============================================================================
// GC Efficiency Tracker
// =============================================================================

class GCEfficiencyTracker {
public:
    struct Sample {
        double durationMs;
        size_t bytesReclaimed;
        double efficiency;  // bytes/ms
    };

    void record(double durationMs, size_t bytesReclaimed) {
        Sample s;
        s.durationMs = durationMs;
        s.bytesReclaimed = bytesReclaimed;
        s.efficiency = durationMs > 0
            ? static_cast<double>(bytesReclaimed) / durationMs : 0;

        history_.push_back(s);
        if (history_.size() > MAX_HISTORY) {
            history_.pop_front();
        }

        // Update smoothed efficiency
        if (smoothedEfficiency_ < 0) {
            smoothedEfficiency_ = s.efficiency;
        } else {
            smoothedEfficiency_ = ALPHA * s.efficiency +
                                  (1.0 - ALPHA) * smoothedEfficiency_;
        }
    }

    double bytesPerMs() const {
        return smoothedEfficiency_ > 0 ? smoothedEfficiency_ : 0;
    }

    double averagePauseMs() const {
        if (history_.empty()) return 0;
        double total = 0;
        for (const auto& s : history_) total += s.durationMs;
        return total / static_cast<double>(history_.size());
    }

    double lastPauseMs() const {
        return history_.empty() ? 0 : history_.back().durationMs;
    }

private:
    static constexpr double ALPHA = 0.3;
    static constexpr size_t MAX_HISTORY = 50;
    std::deque<Sample> history_;
    double smoothedEfficiency_ = -1;
};

// =============================================================================
// Mutator Utilization Tracker
// =============================================================================

class MutatorUtilizationTracker {
public:
    void recordMutatorTime(double ms) { mutatorMs_ += ms; }
    void recordGCTime(double ms) { gcMs_ += ms; }

    double utilization() const {
        double total = mutatorMs_ + gcMs_;
        return total > 0 ? mutatorMs_ / total : 1.0;
    }

    /**
     * @brief Minimum mutator utilization over a sliding window
     *
     * The MMU is the worst-case mutator utilization over any
     * window of the given size. Lower = worse GC pauses.
     */
    double mmu(double windowMs) const {
        // Simplified: use overall utilization as approximation
        (void)windowMs;
        return utilization();
    }

    void reset() { mutatorMs_ = 0; gcMs_ = 0; }

private:
    double mutatorMs_ = 0;
    double gcMs_ = 0;
};

// =============================================================================
// Heap Controller V2
// =============================================================================

class HeapControllerV2 {
public:
    struct Config {
        size_t minHeapSize;
        size_t maxHeapSize;
        size_t initialHeapSize;

        // Growth factor range
        double minGrowthFactor;
        double maxGrowthFactor;

        // Trigger thresholds
        double majorGCOccupancy;    // Trigger major GC at this occupancy
        double compactFragmentation;// Trigger compaction at this frag level
        double emergencyOccupancy;  // Trigger full GC

        // Mutator utilization target
        double targetMutatorUtilization;

        // Nursery sizing
        size_t nurseryMinSize;
        size_t nurseryMaxSize;

        Config()
            : minHeapSize(4 * 1024 * 1024)       // 4MB
            , maxHeapSize(2048ULL * 1024 * 1024)  // 2GB
            , initialHeapSize(32 * 1024 * 1024)   // 32MB
            , minGrowthFactor(1.2)
            , maxGrowthFactor(4.0)
            , majorGCOccupancy(0.75)
            , compactFragmentation(0.30)
            , emergencyOccupancy(0.95)
            , targetMutatorUtilization(0.95)
            , nurseryMinSize(1 * 1024 * 1024)     // 1MB
            , nurseryMaxSize(32 * 1024 * 1024) {} // 32MB
    };

    struct HeapState {
        size_t capacity;
        size_t used;
        size_t nurseryUsed;
        size_t nurseryCapacity;
        size_t oldGenUsed;
        size_t oldGenCapacity;
        double fragmentation;
    };

    struct Decision {
        enum class Action {
            None,
            MinorGC,
            MajorGC,
            CompactGC,
            FullGC,
            GrowHeap,
            ShrinkHeap,
        };
        Action action;
        size_t newHeapSize;     // For Grow/Shrink
        const char* reason;
    };

    explicit HeapControllerV2(const Config& config = Config{})
        : config_(config)
        , currentCapacity_(config.initialHeapSize)
        , currentThreshold_(config.initialHeapSize)
        , nurserySize_(config.nurseryMinSize) {}

    /**
     * @brief Record an allocation
     */
    void onAllocate(size_t bytes) {
        allocTracker_.recordAllocation(bytes);
        totalAllocatedSinceGC_ += bytes;
    }

    /**
     * @brief Record GC cycle completion
     */
    void onGCComplete(double durationMs, size_t bytesReclaimed,
                       size_t liveBytes) {
        gcEfficiency_.record(durationMs, bytesReclaimed);
        mutatorUtil_.recordGCTime(durationMs);
        lastLiveBytes_ = liveBytes;
        totalAllocatedSinceGC_ = 0;

        // Compute new threshold
        computeNewThreshold(liveBytes);
    }

    /**
     * @brief Decide what to do given current heap state
     */
    Decision decide(const HeapState& state) {
        Decision decision;
        decision.action = Decision::Action::None;
        decision.newHeapSize = 0;
        decision.reason = "none";

        double occupancy = state.capacity > 0
            ? static_cast<double>(state.used) /
              static_cast<double>(state.capacity)
            : 0;

        // Emergency: heap nearly full
        if (occupancy >= config_.emergencyOccupancy) {
            decision.action = Decision::Action::FullGC;
            decision.reason = "emergency occupancy";
            return decision;
        }

        // Nursery full → minor GC
        if (state.nurseryCapacity > 0 &&
            state.nurseryUsed >= state.nurseryCapacity * 0.90) {
            decision.action = Decision::Action::MinorGC;
            decision.reason = "nursery full";
            return decision;
        }

        // Old gen threshold → major GC
        if (state.oldGenUsed >= currentThreshold_) {
            // Check fragmentation for compaction
            if (state.fragmentation > config_.compactFragmentation) {
                decision.action = Decision::Action::CompactGC;
                decision.reason = "old gen threshold + fragmentation";
            } else {
                decision.action = Decision::Action::MajorGC;
                decision.reason = "old gen threshold";
            }
            return decision;
        }

        // Predictive: time-to-full too short
        size_t freeBytes = state.capacity > state.used
                           ? state.capacity - state.used : 0;
        double timeToFull = allocTracker_.predictTimeToFull(freeBytes);
        double avgGCTime = gcEfficiency_.averagePauseMs();

        if (timeToFull < avgGCTime * 2.0 && timeToFull < 5000) {
            decision.action = Decision::Action::MajorGC;
            decision.reason = "predictive: time-to-full < 2x GC time";
            return decision;
        }

        // Shrink opportunity
        if (occupancy < 0.25 && state.capacity > config_.minHeapSize * 2) {
            size_t newSize = std::max(
                state.used * 2, config_.minHeapSize);
            if (newSize < state.capacity * 0.75) {
                decision.action = Decision::Action::ShrinkHeap;
                decision.newHeapSize = newSize;
                decision.reason = "low occupancy shrink";
                return decision;
            }
        }

        return decision;
    }

    /**
     * @brief Compute adaptive growth factor
     *
     * Adapts based on GC frequency and mutator utilization:
     * - If GC is too frequent → grow faster
     * - If mutator utilization is low → grow faster
     * - If plenty of memory → slower growth
     */
    double computeGrowthFactor() const {
        double util = mutatorUtil_.utilization();

        if (util < config_.targetMutatorUtilization * 0.8) {
            // GC is taking too much time → aggressive growth
            return config_.maxGrowthFactor;
        }

        if (util > config_.targetMutatorUtilization) {
            // Good utilization → moderate growth
            return config_.minGrowthFactor;
        }

        // Interpolate between min and max
        double t = (config_.targetMutatorUtilization - util) /
                   (config_.targetMutatorUtilization * 0.2);
        return config_.minGrowthFactor +
               t * (config_.maxGrowthFactor - config_.minGrowthFactor);
    }

    /**
     * @brief Compute adaptive nursery size
     *
     * Larger nursery = fewer minor GCs but longer minor pauses.
     * Adjusts based on promotion rate.
     */
    size_t computeNurserySize(double promotionRate) const {
        // promotionRate = fraction of nursery objects promoted to old gen
        // High promotion rate → smaller nursery (objects are long-lived)
        // Low promotion rate → larger nursery (objects die young)

        size_t base = nurserySize_;

        if (promotionRate > 0.5) {
            // Most objects survive → nursery too large
            base = base * 3 / 4;
        } else if (promotionRate < 0.1) {
            // Most objects die → nursery could be larger
            base = base * 5 / 4;
        }

        return std::clamp(base, config_.nurseryMinSize,
                           config_.nurseryMaxSize);
    }

    // Accessors
    size_t currentThreshold() const { return currentThreshold_; }
    size_t currentCapacity() const { return currentCapacity_; }
    double allocationRate() const { return allocTracker_.bytesPerMs(); }
    double gcEfficiency() const { return gcEfficiency_.bytesPerMs(); }
    double mutatorUtilization() const { return mutatorUtil_.utilization(); }

private:
    void computeNewThreshold(size_t liveBytes) {
        double growth = computeGrowthFactor();
        size_t newThreshold = static_cast<size_t>(
            static_cast<double>(liveBytes) * growth);

        newThreshold = std::clamp(newThreshold,
            config_.minHeapSize, config_.maxHeapSize);

        currentThreshold_ = newThreshold;

        // Possibly grow capacity to accommodate
        if (newThreshold > currentCapacity_) {
            currentCapacity_ = std::min(newThreshold * 2,
                                         config_.maxHeapSize);
        }
    }

    Config config_;
    AllocationRateTracker allocTracker_;
    GCEfficiencyTracker gcEfficiency_;
    MutatorUtilizationTracker mutatorUtil_;

    size_t currentCapacity_;
    size_t currentThreshold_;
    size_t nurserySize_;
    size_t lastLiveBytes_ = 0;
    size_t totalAllocatedSinceGC_ = 0;
};

} // namespace Zepra::Heap
