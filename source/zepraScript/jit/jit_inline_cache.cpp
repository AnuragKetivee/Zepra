// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — jit_inline_cache.cpp — Mono/poly/megamorphic inline caches

#include <cstdint>
#include <cassert>
#include <cstring>
#include <vector>
#include <atomic>
#include <mutex>

namespace Zepra::JIT {

using ShapeId = uint32_t;
static constexpr size_t kPolyLimit = 4;   // Max polymorphic entries before megamorphic

enum class ICState : uint8_t {
    Uninitialized,
    Monomorphic,
    Polymorphic,
    Megamorphic,
};

struct ICEntry {
    ShapeId shape;
    uint16_t propertyOffset;
    bool isInline;
    uint8_t* stubCode;       // Cached stub for this shape
    uint32_t hits;

    ICEntry() : shape(0), propertyOffset(0), isInline(false)
        , stubCode(nullptr), hits(0) {}
};

class InlineCache {
public:
    InlineCache() : state_(ICState::Uninitialized), entryCount_(0)
        , totalHits_(0), totalMisses_(0) {}

    ICState state() const { return state_; }

    // Try to look up the property offset for a given shape (fast path).
    bool tryLookup(ShapeId shape, uint16_t& offset) {
        switch (state_) {
            case ICState::Uninitialized:
                totalMisses_++;
                return false;

            case ICState::Monomorphic:
                if (entries_[0].shape == shape) {
                    offset = entries_[0].propertyOffset;
                    entries_[0].hits++;
                    totalHits_++;
                    return true;
                }
                totalMisses_++;
                return false;

            case ICState::Polymorphic:
                for (size_t i = 0; i < entryCount_; i++) {
                    if (entries_[i].shape == shape) {
                        offset = entries_[i].propertyOffset;
                        entries_[i].hits++;
                        totalHits_++;
                        return true;
                    }
                }
                totalMisses_++;
                return false;

            case ICState::Megamorphic:
                totalMisses_++;
                return false;
        }
        return false;
    }

    // Update the IC with a new shape → offset mapping (slow path).
    void update(ShapeId shape, uint16_t offset, bool isInline) {
        switch (state_) {
            case ICState::Uninitialized:
                entries_[0].shape = shape;
                entries_[0].propertyOffset = offset;
                entries_[0].isInline = isInline;
                entries_[0].hits = 1;
                entryCount_ = 1;
                state_ = ICState::Monomorphic;
                break;

            case ICState::Monomorphic:
                if (entries_[0].shape == shape) return;
                entries_[1].shape = shape;
                entries_[1].propertyOffset = offset;
                entries_[1].isInline = isInline;
                entries_[1].hits = 1;
                entryCount_ = 2;
                state_ = ICState::Polymorphic;
                break;

            case ICState::Polymorphic:
                // Check duplicate.
                for (size_t i = 0; i < entryCount_; i++) {
                    if (entries_[i].shape == shape) return;
                }
                if (entryCount_ < kPolyLimit) {
                    entries_[entryCount_].shape = shape;
                    entries_[entryCount_].propertyOffset = offset;
                    entries_[entryCount_].isInline = isInline;
                    entries_[entryCount_].hits = 1;
                    entryCount_++;
                } else {
                    state_ = ICState::Megamorphic;
                }
                break;

            case ICState::Megamorphic:
                break;
        }
    }

    // Reset the IC (after deoptimization or GC).
    void reset() {
        state_ = ICState::Uninitialized;
        entryCount_ = 0;
        totalHits_ = 0;
        totalMisses_ = 0;
    }

    uint64_t hits() const { return totalHits_; }
    uint64_t misses() const { return totalMisses_; }
    double hitRate() const {
        uint64_t total = totalHits_ + totalMisses_;
        return total ? static_cast<double>(totalHits_) / total : 0.0;
    }

private:
    ICState state_;
    ICEntry entries_[kPolyLimit];
    uint8_t entryCount_;
    uint64_t totalHits_;
    uint64_t totalMisses_;
};

// IC site registry — tracks all ICs for a function.
class ICSiteRegistry {
public:
    uint32_t createSite() {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t id = static_cast<uint32_t>(sites_.size());
        sites_.push_back({});
        return id;
    }

    InlineCache* getSite(uint32_t id) {
        return id < sites_.size() ? &sites_[id] : nullptr;
    }

    void resetAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& site : sites_) site.reset();
    }

    size_t siteCount() const { return sites_.size(); }

    struct AggregateStats {
        size_t mono = 0;
        size_t poly = 0;
        size_t mega = 0;
        size_t uninit = 0;
        double avgHitRate = 0;
    };

    AggregateStats aggregate() const {
        AggregateStats stats;
        double totalRate = 0;
        for (auto& site : sites_) {
            switch (site.state()) {
                case ICState::Uninitialized: stats.uninit++; break;
                case ICState::Monomorphic: stats.mono++; break;
                case ICState::Polymorphic: stats.poly++; break;
                case ICState::Megamorphic: stats.mega++; break;
            }
            totalRate += site.hitRate();
        }
        if (!sites_.empty()) stats.avgHitRate = totalRate / sites_.size();
        return stats;
    }

private:
    std::mutex mutex_;
    std::vector<InlineCache> sites_;
};

} // namespace Zepra::JIT
