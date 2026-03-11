// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_histogram.cpp — Pause-time and allocation histograms

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <vector>
#include <memory>

namespace Zepra::Heap {

class Histogram {
public:
    static constexpr size_t kMaxBuckets = 64;

    struct Bucket {
        double lowerBound;
        double upperBound;
        uint64_t count;
        double sum;

        Bucket() : lowerBound(0), upperBound(0), count(0), sum(0) {}
    };

    // Create a linear histogram with `numBuckets` buckets from `min` to `max`.
    static Histogram linear(const char* name, double min, double max, size_t numBuckets) {
        Histogram h;
        h.name_ = name;
        h.numBuckets_ = std::min(numBuckets, kMaxBuckets);
        double step = (max - min) / h.numBuckets_;
        for (size_t i = 0; i < h.numBuckets_; i++) {
            h.buckets_[i].lowerBound = min + i * step;
            h.buckets_[i].upperBound = min + (i + 1) * step;
        }
        return h;
    }

    // Create an exponential histogram: bucket i covers [base^i, base^(i+1)).
    static Histogram exponential(const char* name, double base, size_t numBuckets) {
        Histogram h;
        h.name_ = name;
        h.numBuckets_ = std::min(numBuckets, kMaxBuckets);
        for (size_t i = 0; i < h.numBuckets_; i++) {
            h.buckets_[i].lowerBound = std::pow(base, static_cast<double>(i));
            h.buckets_[i].upperBound = std::pow(base, static_cast<double>(i + 1));
        }
        return h;
    }

    Histogram() : name_(""), numBuckets_(0), totalCount_(0), totalSum_(0)
        , min_(1e18), max_(0), mutex_(std::make_unique<std::mutex>()) {}

    void record(double value) {
        std::lock_guard<std::mutex> lock(*mutex_);
        totalCount_++;
        totalSum_ += value;
        if (value < min_) min_ = value;
        if (value > max_) max_ = value;

        for (size_t i = 0; i < numBuckets_; i++) {
            if (value >= buckets_[i].lowerBound && value < buckets_[i].upperBound) {
                buckets_[i].count++;
                buckets_[i].sum += value;
                return;
            }
        }

        // Overflow: put in last bucket.
        if (numBuckets_ > 0) {
            buckets_[numBuckets_ - 1].count++;
            buckets_[numBuckets_ - 1].sum += value;
        }
    }

    const char* name() const { return name_; }
    uint64_t totalCount() const { return totalCount_; }
    double totalSum() const { return totalSum_; }
    double min() const { return min_; }
    double max() const { return max_; }

    double mean() const {
        return totalCount_ > 0 ? totalSum_ / totalCount_ : 0;
    }

    // Percentile from buckets (linear interpolation).
    double percentile(double p) const {
        std::lock_guard<std::mutex> lock(*mutex_);
        if (totalCount_ == 0) return 0;

        uint64_t target = static_cast<uint64_t>(p / 100.0 * totalCount_);
        uint64_t cumulative = 0;

        for (size_t i = 0; i < numBuckets_; i++) {
            cumulative += buckets_[i].count;
            if (cumulative >= target) {
                double ratio = buckets_[i].count > 0
                    ? static_cast<double>(target - (cumulative - buckets_[i].count)) / buckets_[i].count
                    : 0;
                return buckets_[i].lowerBound +
                    ratio * (buckets_[i].upperBound - buckets_[i].lowerBound);
            }
        }

        return max_;
    }

    const Bucket* bucket(size_t idx) const {
        return idx < numBuckets_ ? &buckets_[idx] : nullptr;
    }

    size_t numBuckets() const { return numBuckets_; }

    void reset() {
        std::lock_guard<std::mutex> lock(*mutex_);
        for (size_t i = 0; i < numBuckets_; i++) {
            buckets_[i].count = 0;
            buckets_[i].sum = 0;
        }
        totalCount_ = 0;
        totalSum_ = 0;
        min_ = 1e18;
        max_ = 0;
    }

private:
    const char* name_;
    size_t numBuckets_;
    Bucket buckets_[kMaxBuckets];
    uint64_t totalCount_;
    double totalSum_;
    double min_;
    double max_;
    mutable std::unique_ptr<std::mutex> mutex_;
};

// Pre-defined histograms for GC.
class GCHistograms {
public:
    GCHistograms()
        : pauseTime_(Histogram::exponential("gc_pause_ms", 2.0, 20))       // 1ms → 1M ms
        , allocSize_(Histogram::exponential("alloc_size_bytes", 2.0, 20))   // 1B → 1MB
        , survivalRate_(Histogram::linear("survival_rate", 0, 1.0, 20))
        , fragmentation_(Histogram::linear("fragmentation", 0, 1.0, 20))
        , nurseryPromotionRate_(Histogram::linear("nursery_promo_rate", 0, 1.0, 20)) {}

    Histogram& pauseTime() { return pauseTime_; }
    Histogram& allocSize() { return allocSize_; }
    Histogram& survivalRate() { return survivalRate_; }
    Histogram& fragmentation() { return fragmentation_; }
    Histogram& nurseryPromotionRate() { return nurseryPromotionRate_; }

    void recordPause(double ms) { pauseTime_.record(ms); }
    void recordAlloc(size_t bytes) { allocSize_.record(static_cast<double>(bytes)); }
    void recordSurvival(double rate) { survivalRate_.record(rate); }
    void recordFragmentation(double f) { fragmentation_.record(f); }

    void resetAll() {
        pauseTime_.reset();
        allocSize_.reset();
        survivalRate_.reset();
        fragmentation_.reset();
        nurseryPromotionRate_.reset();
    }

private:
    Histogram pauseTime_;
    Histogram allocSize_;
    Histogram survivalRate_;
    Histogram fragmentation_;
    Histogram nurseryPromotionRate_;
};

} // namespace Zepra::Heap
