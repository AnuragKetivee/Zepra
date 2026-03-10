// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_generation_balancer.cpp — Dynamic nursery/old-gen ratio tuning

#include <mutex>
#include <functional>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <algorithm>

namespace Zepra::Heap {

class GenerationBalancer {
public:
    struct Config {
        size_t minNurserySize;
        size_t maxNurserySize;
        uint8_t minPromotionAge;
        uint8_t maxPromotionAge;
        double targetSurvivalRatio;   // Ideal young-gen survival rate
        uint32_t samplingWindow;      // Number of collections to average over

        Config()
            : minNurserySize(2 * 1024 * 1024)
            , maxNurserySize(16 * 1024 * 1024)
            , minPromotionAge(2)
            , maxPromotionAge(8)
            , targetSurvivalRatio(0.15)
            , samplingWindow(8) {}
    };

    struct Callbacks {
        std::function<void(size_t newNurserySize)> resizeNursery;
        std::function<void(uint8_t newAge)> setPromotionAge;
        std::function<size_t()> getCurrentNurseryUsed;
        std::function<size_t()> getOldGenUsed;
    };

    explicit GenerationBalancer(const Config& config = Config{})
        : config_(config)
        , nurserySize_(config.minNurserySize)
        , promotionAge_(config.minPromotionAge) {}

    void setCallbacks(Callbacks cb) { cb_ = std::move(cb); }

    // Called after each minor GC with collection results.
    void recordMinorGC(size_t allocated, size_t survived, size_t promoted) {
        std::lock_guard<std::mutex> lock(mutex_);

        totalAllocated_ += allocated;
        totalSurvived_ += survived;
        totalPromoted_ += promoted;
        collectionCount_++;

        double survivalRatio = allocated > 0
            ? static_cast<double>(survived) / allocated : 0;

        // Update rolling window.
        survivalHistory_[historyIdx_ % config_.samplingWindow] = survivalRatio;
        historyIdx_++;

        // Rebalance after enough data points.
        if (collectionCount_ % config_.samplingWindow == 0) {
            rebalance();
        }
    }

    void rebalance() {
        double avgSurvival = averageSurvival();

        // Nursery sizing: high survival → larger nursery (objects need more time).
        // Low survival → smaller nursery is fine, frees old-gen pressure.
        if (avgSurvival > config_.targetSurvivalRatio * 2.0) {
            size_t newSize = std::min(nurserySize_ * 2, config_.maxNurserySize);
            if (newSize != nurserySize_) {
                nurserySize_ = newSize;
                if (cb_.resizeNursery) cb_.resizeNursery(nurserySize_);
                stats_.nurseryResizes++;
            }
        } else if (avgSurvival < config_.targetSurvivalRatio * 0.5 &&
                   nurserySize_ > config_.minNurserySize) {
            size_t newSize = std::max(nurserySize_ / 2, config_.minNurserySize);
            if (newSize != nurserySize_) {
                nurserySize_ = newSize;
                if (cb_.resizeNursery) cb_.resizeNursery(nurserySize_);
                stats_.nurseryResizes++;
            }
        }

        // Promotion age tuning: high survival → increase age (delay promotion).
        // Low survival → decrease age (promote earlier, reduce nursery churn).
        if (avgSurvival > config_.targetSurvivalRatio * 1.5) {
            uint8_t newAge = std::min<uint8_t>(promotionAge_ + 1, config_.maxPromotionAge);
            if (newAge != promotionAge_) {
                promotionAge_ = newAge;
                if (cb_.setPromotionAge) cb_.setPromotionAge(promotionAge_);
                stats_.ageAdjustments++;
            }
        } else if (avgSurvival < config_.targetSurvivalRatio * 0.5) {
            uint8_t newAge = std::max<uint8_t>(
                promotionAge_ > 0 ? promotionAge_ - 1 : 0, config_.minPromotionAge);
            if (newAge != promotionAge_) {
                promotionAge_ = newAge;
                if (cb_.setPromotionAge) cb_.setPromotionAge(promotionAge_);
                stats_.ageAdjustments++;
            }
        }
    }

    size_t nurserySize() const { return nurserySize_; }
    uint8_t promotionAge() const { return promotionAge_; }

    struct Stats {
        uint64_t nurseryResizes;
        uint64_t ageAdjustments;
    };
    Stats stats() const { return stats_; }

private:
    double averageSurvival() const {
        size_t count = std::min<size_t>(historyIdx_, config_.samplingWindow);
        if (count == 0) return 0;
        double sum = 0;
        for (size_t i = 0; i < count; i++) {
            sum += survivalHistory_[i];
        }
        return sum / count;
    }

    Config config_;
    Callbacks cb_;
    size_t nurserySize_;
    uint8_t promotionAge_;

    double survivalHistory_[32] = {};
    size_t historyIdx_ = 0;
    uint64_t collectionCount_ = 0;
    uint64_t totalAllocated_ = 0;
    uint64_t totalSurvived_ = 0;
    uint64_t totalPromoted_ = 0;

    Stats stats_{};
    mutable std::mutex mutex_;
};

} // namespace Zepra::Heap
