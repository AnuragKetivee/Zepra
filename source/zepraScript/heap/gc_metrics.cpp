// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file gc_metrics.cpp
 * @brief GC telemetry, reporting, and dashboard data
 *
 * Collects and aggregates GC metrics for DevTools, logging,
 * and adaptive scheduling decisions.
 *
 * Metrics tracked:
 * - Pause times (min, max, p50, p95, p99)
 * - Allocation rate (bytes/s, objects/s)
 * - Collection frequency
 * - Memory utilization (live/total by generation)
 * - Fragmentation ratio
 * - Promotion rate (nursery → old gen)
 * - Throughput (% time in mutator vs GC)
 * - Page utilization histogram
 */

#include <atomic>
#include <mutex>
#include <vector>
#include <deque>
#include <string>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <cmath>

namespace Zepra::Heap {

// =============================================================================
// Percentile Tracker
// =============================================================================

class PercentileTracker {
public:
    explicit PercentileTracker(size_t maxSamples = 1000)
        : maxSamples_(maxSamples) {}

    void record(double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.push_back(value);
        if (samples_.size() > maxSamples_) samples_.pop_front();
        sorted_.clear();
    }

    double percentile(double p) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.empty()) return 0;

        if (sorted_.empty()) {
            sorted_.assign(samples_.begin(), samples_.end());
            std::sort(sorted_.begin(), sorted_.end());
        }

        size_t idx = static_cast<size_t>(p / 100.0 * static_cast<double>(sorted_.size() - 1));
        return sorted_[idx];
    }

    double min() const { return percentile(0); }
    double max() const { return percentile(100); }
    double p50() const { return percentile(50); }
    double p95() const { return percentile(95); }
    double p99() const { return percentile(99); }

    double mean() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.empty()) return 0;
        double sum = 0;
        for (double v : samples_) sum += v;
        return sum / static_cast<double>(samples_.size());
    }

    double stddev() const {
        double m = mean();
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.size() < 2) return 0;
        double sumSq = 0;
        for (double v : samples_) sumSq += (v - m) * (v - m);
        return std::sqrt(sumSq / static_cast<double>(samples_.size() - 1));
    }

    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
        sorted_.clear();
    }

private:
    size_t maxSamples_;
    std::deque<double> samples_;
    mutable std::vector<double> sorted_;
    mutable std::mutex mutex_;
};

// =============================================================================
// Rolling Counter
// =============================================================================

class RollingCounter {
public:
    explicit RollingCounter(size_t windowMs = 5000, size_t buckets = 50)
        : windowMs_(windowMs), buckets_(buckets) {
        bucketMs_ = windowMs / buckets;
        counts_.resize(buckets, 0);
    }

    void increment(size_t amount = 1) {
        advanceTo(nowMs());
        counts_[currentBucket_] += amount;
    }

    size_t sum() const {
        size_t total = 0;
        for (auto c : counts_) total += c;
        return total;
    }

    double ratePerSecond() const {
        return static_cast<double>(sum()) / (static_cast<double>(windowMs_) / 1000.0);
    }

    void clear() {
        for (auto& c : counts_) c = 0;
    }

private:
    void advanceTo(uint64_t nowMs) {
        if (lastMs_ == 0) { lastMs_ = nowMs; return; }
        uint64_t elapsed = nowMs - lastMs_;
        size_t bucketsToAdvance = static_cast<size_t>(elapsed / bucketMs_);

        if (bucketsToAdvance > 0) {
            bucketsToAdvance = std::min(bucketsToAdvance, buckets_);
            for (size_t i = 0; i < bucketsToAdvance; i++) {
                currentBucket_ = (currentBucket_ + 1) % buckets_;
                counts_[currentBucket_] = 0;
            }
            lastMs_ = nowMs;
        }
    }

    static uint64_t nowMs() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    size_t windowMs_;
    size_t buckets_;
    size_t bucketMs_;
    size_t currentBucket_ = 0;
    uint64_t lastMs_ = 0;
    std::vector<size_t> counts_;
};

// =============================================================================
// GC Metrics Collector
// =============================================================================

class GCMetricsCollector {
public:
    struct GenerationMetrics {
        size_t usedBytes = 0;
        size_t capacityBytes = 0;
        size_t peakBytes = 0;
        uint64_t allocations = 0;
        uint64_t collections = 0;
        double avgPauseMs = 0;
    };

    struct HeapMetrics {
        GenerationMetrics nursery;
        GenerationMetrics oldGen;
        GenerationMetrics los;

        size_t totalUsed() const {
            return nursery.usedBytes + oldGen.usedBytes + los.usedBytes;
        }
        size_t totalCapacity() const {
            return nursery.capacityBytes + oldGen.capacityBytes + los.capacityBytes;
        }

        double fragmentation;
        double mutatorUtilization;
        double promotionRate;
        double allocationRateBytesPerSec;
    };

    GCMetricsCollector() = default;

    // Record events
    void recordMinorGCPause(double ms) {
        minorPauses_.record(ms);
        minorGCCount_.increment();
    }

    void recordMajorGCPause(double ms) {
        majorPauses_.record(ms);
        majorGCCount_.increment();
    }

    void recordAllocation(size_t bytes) {
        allocationBytes_.increment(bytes);
        allocationCount_.increment();
    }

    void recordPromotion(size_t bytes) {
        promotionBytes_.increment(bytes);
    }

    void updateGenerationMetrics(const HeapMetrics& metrics) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastMetrics_ = metrics;
    }

    // Query metrics
    const PercentileTracker& minorPauses() const { return minorPauses_; }
    const PercentileTracker& majorPauses() const { return majorPauses_; }

    double allocationRateBytesPerSec() const { return allocationBytes_.ratePerSecond(); }
    double allocationRateObjPerSec() const { return allocationCount_.ratePerSecond(); }
    double promotionRateBytesPerSec() const { return promotionBytes_.ratePerSecond(); }
    double minorGCRatePerSec() const { return minorGCCount_.ratePerSecond(); }
    double majorGCRatePerSec() const { return majorGCCount_.ratePerSecond(); }

    HeapMetrics currentMetrics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastMetrics_;
    }

    /**
     * @brief Compute throughput (mutator time / total time)
     */
    double throughput(double windowSeconds = 60.0) const {
        double totalPauseMs = minorPauses_.mean() *
                              static_cast<double>(minorPauses_.count()) +
                              majorPauses_.mean() *
                              static_cast<double>(majorPauses_.count());
        double windowMs = windowSeconds * 1000.0;
        if (windowMs <= 0) return 1.0;
        double gcPercent = totalPauseMs / windowMs;
        return std::max(0.0, 1.0 - gcPercent);
    }

    // Export
    std::string toJSON() const;
    std::string toCSVHeader() const;
    std::string toCSVRow() const;

    void clear() {
        minorPauses_.clear();
        majorPauses_.clear();
        allocationBytes_.clear();
        allocationCount_.clear();
        promotionBytes_.clear();
        minorGCCount_.clear();
        majorGCCount_.clear();
    }

private:
    PercentileTracker minorPauses_{500};
    PercentileTracker majorPauses_{200};
    RollingCounter allocationBytes_{5000, 50};
    RollingCounter allocationCount_{5000, 50};
    RollingCounter promotionBytes_{5000, 50};
    RollingCounter minorGCCount_{60000, 60};
    RollingCounter majorGCCount_{60000, 60};
    HeapMetrics lastMetrics_{};
    mutable std::mutex mutex_;
};

inline std::string GCMetricsCollector::toJSON() const {
    char buf[2048];
    auto m = currentMetrics();
    snprintf(buf, sizeof(buf),
        "{\n"
        "  \"heap\": {\"used\": %zu, \"capacity\": %zu, \"fragmentation\": %.3f},\n"
        "  \"nursery\": {\"used\": %zu, \"capacity\": %zu},\n"
        "  \"oldGen\": {\"used\": %zu, \"capacity\": %zu},\n"
        "  \"los\": {\"used\": %zu, \"capacity\": %zu},\n"
        "  \"pauses\": {\n"
        "    \"minor\": {\"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f, \"count\": %zu},\n"
        "    \"major\": {\"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f, \"count\": %zu}\n"
        "  },\n"
        "  \"rates\": {\n"
        "    \"allocationBytesPerSec\": %.0f,\n"
        "    \"promotionBytesPerSec\": %.0f,\n"
        "    \"minorGCPerSec\": %.2f,\n"
        "    \"majorGCPerSec\": %.2f\n"
        "  },\n"
        "  \"throughput\": %.4f\n"
        "}\n",
        m.totalUsed(), m.totalCapacity(), m.fragmentation,
        m.nursery.usedBytes, m.nursery.capacityBytes,
        m.oldGen.usedBytes, m.oldGen.capacityBytes,
        m.los.usedBytes, m.los.capacityBytes,
        minorPauses_.p50(), minorPauses_.p95(), minorPauses_.p99(), minorPauses_.count(),
        majorPauses_.p50(), majorPauses_.p95(), majorPauses_.p99(), majorPauses_.count(),
        allocationRateBytesPerSec(), promotionRateBytesPerSec(),
        minorGCRatePerSec(), majorGCRatePerSec(),
        throughput());
    return std::string(buf);
}

inline std::string GCMetricsCollector::toCSVHeader() const {
    return "timestamp_ms,heap_used,heap_capacity,nursery_used,oldgen_used,"
           "los_used,fragmentation,minor_p50,minor_p95,major_p50,major_p95,"
           "alloc_rate,promo_rate,throughput\n";
}

inline std::string GCMetricsCollector::toCSVRow() const {
    auto m = currentMetrics();
    uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    char buf[512];
    snprintf(buf, sizeof(buf),
        "%lu,%zu,%zu,%zu,%zu,%zu,%.3f,%.2f,%.2f,%.2f,%.2f,%.0f,%.0f,%.4f\n",
        static_cast<unsigned long>(now),
        m.totalUsed(), m.totalCapacity(),
        m.nursery.usedBytes, m.oldGen.usedBytes, m.los.usedBytes,
        m.fragmentation,
        minorPauses_.p50(), minorPauses_.p95(),
        majorPauses_.p50(), majorPauses_.p95(),
        allocationRateBytesPerSec(), promotionRateBytesPerSec(),
        throughput());
    return std::string(buf);
}

} // namespace Zepra::Heap
