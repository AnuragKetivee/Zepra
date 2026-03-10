// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_statistics_collector.cpp — Per-GC-cycle stats: phase durations, bytes, ratios

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <mutex>
#include <vector>
#include <chrono>
#include <algorithm>
#include <atomic>

namespace Zepra::Heap {

struct GCCycleStats {
    uint64_t cycleId;
    uint64_t timestampMs;
    double totalDurationMs;
    double markDurationMs;
    double sweepDurationMs;
    double compactDurationMs;
    double finalizeDurationMs;
    size_t heapBefore;
    size_t heapAfter;
    size_t liveBytesAfter;
    size_t freedBytes;
    size_t promotedBytes;
    size_t nurseryCollected;
    uint32_t arenasSurveyed;
    uint32_t arenasReleased;
    double fragmentationRatio;
    double survivalRatio;
    bool wasCompacting;
    bool wasConcurrent;

    GCCycleStats() : cycleId(0), timestampMs(0), totalDurationMs(0), markDurationMs(0)
        , sweepDurationMs(0), compactDurationMs(0), finalizeDurationMs(0)
        , heapBefore(0), heapAfter(0), liveBytesAfter(0), freedBytes(0)
        , promotedBytes(0), nurseryCollected(0), arenasSurveyed(0), arenasReleased(0)
        , fragmentationRatio(0), survivalRatio(0), wasCompacting(false), wasConcurrent(false) {}
};

class GCStatisticsCollector {
public:
    static constexpr size_t kMaxHistory = 256;

    GCStatisticsCollector() : nextCycleId_(1) {}

    // Begin a new GC cycle — returns the cycle ID.
    uint64_t beginCycle(size_t heapBytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = {};
        current_.cycleId = nextCycleId_++;
        current_.heapBefore = heapBytes;
        current_.timestampMs = currentTimestampMs();
        cycleStart_ = std::chrono::steady_clock::now();
        return current_.cycleId;
    }

    void recordMarkDuration(double ms) { current_.markDurationMs = ms; }
    void recordSweepDuration(double ms) { current_.sweepDurationMs = ms; }
    void recordCompactDuration(double ms) { current_.compactDurationMs = ms; }
    void recordFinalizeDuration(double ms) { current_.finalizeDurationMs = ms; }
    void recordPromotedBytes(size_t bytes) { current_.promotedBytes = bytes; }
    void recordNurseryCollected(size_t bytes) { current_.nurseryCollected = bytes; }
    void recordArenasSurveyed(uint32_t count) { current_.arenasSurveyed = count; }
    void recordArenasReleased(uint32_t count) { current_.arenasReleased = count; }
    void setCompacting(bool v) { current_.wasCompacting = v; }
    void setConcurrent(bool v) { current_.wasConcurrent = v; }

    // End cycle — compute derived stats and archive.
    GCCycleStats endCycle(size_t heapAfter, size_t liveBytes) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto end = std::chrono::steady_clock::now();
        current_.totalDurationMs = std::chrono::duration<double, std::milli>(end - cycleStart_).count();

        current_.heapAfter = heapAfter;
        current_.liveBytesAfter = liveBytes;
        current_.freedBytes = current_.heapBefore > heapAfter ? current_.heapBefore - heapAfter : 0;

        current_.survivalRatio = current_.heapBefore > 0
            ? static_cast<double>(liveBytes) / current_.heapBefore : 0;

        current_.fragmentationRatio = heapAfter > 0
            ? 1.0 - (static_cast<double>(liveBytes) / heapAfter) : 0;

        history_.push_back(current_);
        if (history_.size() > kMaxHistory) {
            history_.erase(history_.begin());
        }

        // Update aggregate counters.
        totalGCCount_++;
        totalFreedBytes_ += current_.freedBytes;
        totalPauseMs_ += current_.totalDurationMs;
        peakPauseMs_ = std::max(peakPauseMs_, current_.totalDurationMs);

        return current_;
    }

    // Aggregate stats.
    uint64_t totalGCCount() const { return totalGCCount_; }
    size_t totalFreedBytes() const { return totalFreedBytes_; }
    double totalPauseMs() const { return totalPauseMs_; }
    double peakPauseMs() const { return peakPauseMs_; }

    double averagePauseMs() const {
        return totalGCCount_ > 0 ? totalPauseMs_ / totalGCCount_ : 0;
    }

    double averageFreedBytes() const {
        return totalGCCount_ > 0 ? static_cast<double>(totalFreedBytes_) / totalGCCount_ : 0;
    }

    const std::vector<GCCycleStats>& history() const { return history_; }

    const GCCycleStats* lastCycle() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return history_.empty() ? nullptr : &history_.back();
    }

    // Percentile pause time from history.
    double percentilePause(double p) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (history_.empty()) return 0;

        std::vector<double> pauses;
        for (auto& c : history_) pauses.push_back(c.totalDurationMs);
        std::sort(pauses.begin(), pauses.end());

        size_t idx = std::min(static_cast<size_t>(p / 100.0 * pauses.size()), pauses.size() - 1);
        return pauses[idx];
    }

private:
    static uint64_t currentTimestampMs() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    mutable std::mutex mutex_;
    GCCycleStats current_;
    std::chrono::steady_clock::time_point cycleStart_;
    std::vector<GCCycleStats> history_;
    uint64_t nextCycleId_;
    uint64_t totalGCCount_ = 0;
    size_t totalFreedBytes_ = 0;
    double totalPauseMs_ = 0;
    double peakPauseMs_ = 0;
};

} // namespace Zepra::Heap
