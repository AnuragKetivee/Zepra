// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file region_allocator.cpp
 * @brief Region-based object allocator
 *
 * Allocates JS objects into heap regions with generational support:
 *
 * Nursery allocation (fast path):
 * - Per-thread TLAB carved from Eden regions
 * - Bump-pointer within TLAB (no synchronization)
 * - TLAB exhaustion → request new TLAB from region allocator
 * - All TLABs exhausted → minor GC
 *
 * Old-gen allocation (slow path):
 * - Called during promotion (minor GC copies survivors here)
 * - Uses per-region bump-pointer with mutex
 * - Region full → allocate new region
 *
 * Large-object allocation:
 * - Objects > LARGE_OBJECT_THRESHOLD get their own region(s)
 * - Allocated directly, not through TLAB
 * - Collected by major GC (not scavenged)
 *
 * Allocation pipeline:
 *   allocate(size)
 *     → tryTLAB(size)                [fast, no lock]
 *       → tryRefillTLAB()            [region lock]
 *         → tryAllocateRegion()      [global lock]
 *           → requestGC()            [last resort]
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
#include <array>

namespace Zepra::Heap {

// =============================================================================
// Constants
// =============================================================================

static constexpr size_t TLAB_DEFAULT_SIZE = 32 * 1024;   // 32KB
static constexpr size_t TLAB_MIN_SIZE = 4 * 1024;        // 4KB
static constexpr size_t TLAB_MAX_SIZE = 256 * 1024;      // 256KB
static constexpr size_t LARGE_OBJECT_THRESHOLD = 128 * 1024; // 128KB
static constexpr size_t OBJECT_ALIGNMENT = 8;
static constexpr size_t REGION_ALLOC_SIZE = 256 * 1024;  // 256KB

// =============================================================================
// Thread-Local Allocation Buffer
// =============================================================================

struct TLAB {
    uintptr_t start;        // Start of this TLAB
    uintptr_t cursor;       // Next free byte
    uintptr_t end;          // End of this TLAB
    uint32_t regionIndex;   // Which region this TLAB is in
    uint32_t threadId;

    size_t remaining() const { return end > cursor ? end - cursor : 0; }
    size_t used() const { return cursor - start; }
    size_t capacity() const { return end - start; }
    bool canFit(size_t size) const { return cursor + size <= end; }

    /**
     * @brief Bump-pointer allocation (no synchronization needed)
     */
    uintptr_t allocate(size_t size) {
        size = (size + OBJECT_ALIGNMENT - 1) & ~(OBJECT_ALIGNMENT - 1);
        if (cursor + size > end) return 0;

        uintptr_t result = cursor;
        cursor += size;
        return result;
    }

    void reset() {
        start = 0;
        cursor = 0;
        end = 0;
        regionIndex = UINT32_MAX;
    }

    bool isValid() const { return start != 0 && end > start; }
};

// =============================================================================
// Region Allocation State
// =============================================================================

/**
 * @brief Per-region allocation state
 *
 * Tracks the allocation cursor within a region for non-TLAB
 * allocations (promotions, large objects).
 */
struct RegionAllocState {
    uint32_t regionIndex;
    uintptr_t base;
    uintptr_t cursor;
    uintptr_t limit;
    std::mutex mutex;
    bool active;

    size_t remaining() const { return limit > cursor ? limit - cursor : 0; }

    uintptr_t allocateLocked(size_t size) {
        size = (size + OBJECT_ALIGNMENT - 1) & ~(OBJECT_ALIGNMENT - 1);
        if (cursor + size > limit) return 0;
        uintptr_t result = cursor;
        cursor += size;
        return result;
    }
};

// =============================================================================
// Region Allocator
// =============================================================================

class RegionAllocator {
public:
    struct Config {
        size_t tlabSize;
        size_t maxEdenRegions;
        size_t maxOldGenRegions;
        size_t maxLargeObjectRegions;

        Config()
            : tlabSize(TLAB_DEFAULT_SIZE)
            , maxEdenRegions(64)
            , maxOldGenRegions(512)
            , maxLargeObjectRegions(64) {}
    };

    struct Stats {
        std::atomic<uint64_t> tlabAllocations{0};
        std::atomic<uint64_t> tlabRefills{0};
        std::atomic<uint64_t> slowPathAllocations{0};
        std::atomic<uint64_t> largeObjectAllocations{0};
        std::atomic<uint64_t> promotions{0};
        std::atomic<uint64_t> bytesAllocated{0};
        std::atomic<uint64_t> bytesPromoted{0};
        std::atomic<uint64_t> allocationFailures{0};
        std::atomic<uint64_t> gcTriggered{0};
    };

    // Callbacks
    struct Callbacks {
        // Request a region from the region table
        std::function<uintptr_t(uint32_t& regionIndex, uint8_t type)>
            allocateRegion;

        // Release a region
        std::function<void(uint32_t regionIndex)> releaseRegion;

        // Trigger GC
        std::function<void(uint8_t gcType)> requestGC;

        // Notify of allocation (for heap controller tracking)
        std::function<void(size_t bytes)> onAllocate;
    };

    explicit RegionAllocator(const Config& config = Config{})
        : config_(config) {}

    void setCallbacks(Callbacks cb) { cb_ = std::move(cb); }

    // -------------------------------------------------------------------------
    // Nursery allocation (fast path)
    // -------------------------------------------------------------------------

    /**
     * @brief Allocate in nursery via TLAB (fast path)
     *
     * This is the hot path called by the interpreter/JIT.
     * No synchronization on the fast path.
     */
    uintptr_t allocateNursery(TLAB& tlab, size_t size) {
        // Fast path: bump pointer in TLAB
        uintptr_t result = tlab.allocate(size);
        if (result != 0) {
            stats_.tlabAllocations.fetch_add(1, std::memory_order_relaxed);
            stats_.bytesAllocated.fetch_add(size, std::memory_order_relaxed);
            if (cb_.onAllocate) cb_.onAllocate(size);
            return result;
        }

        // Slow path: refill TLAB
        return allocateNurserySlow(tlab, size);
    }

    /**
     * @brief Refill TLAB and retry allocation
     */
    uintptr_t allocateNurserySlow(TLAB& tlab, size_t size) {
        std::lock_guard<std::mutex> lock(edenMutex_);

        // Try to carve a new TLAB from the current Eden region
        if (currentEden_ && currentEden_->active) {
            std::lock_guard<std::mutex> rlock(currentEden_->mutex);
            size_t tlabSize = computeTLABSize(tlab);

            uintptr_t tlabBase = currentEden_->allocateLocked(tlabSize);
            if (tlabBase != 0) {
                tlab.start = tlabBase;
                tlab.cursor = tlabBase;
                tlab.end = tlabBase + tlabSize;
                tlab.regionIndex = currentEden_->regionIndex;

                stats_.tlabRefills.fetch_add(1, std::memory_order_relaxed);

                // Retry allocation in the new TLAB
                uintptr_t result = tlab.allocate(size);
                if (result != 0) {
                    stats_.bytesAllocated.fetch_add(size,
                        std::memory_order_relaxed);
                    if (cb_.onAllocate) cb_.onAllocate(size);
                    return result;
                }
            }
        }

        // Current Eden region exhausted → get a new one
        if (!allocateNewEdenRegion()) {
            // No regions available → trigger minor GC
            stats_.gcTriggered.fetch_add(1, std::memory_order_relaxed);
            if (cb_.requestGC) cb_.requestGC(0); // 0 = Minor

            // After GC, retry
            if (!allocateNewEdenRegion()) {
                stats_.allocationFailures.fetch_add(1,
                    std::memory_order_relaxed);
                return 0;
            }
        }

        // Carve TLAB from new region
        {
            std::lock_guard<std::mutex> rlock(currentEden_->mutex);
            size_t tlabSize = computeTLABSize(tlab);

            uintptr_t tlabBase = currentEden_->allocateLocked(tlabSize);
            if (tlabBase != 0) {
                tlab.start = tlabBase;
                tlab.cursor = tlabBase;
                tlab.end = tlabBase + tlabSize;
                tlab.regionIndex = currentEden_->regionIndex;

                uintptr_t result = tlab.allocate(size);
                if (result != 0) {
                    stats_.tlabRefills.fetch_add(1,
                        std::memory_order_relaxed);
                    stats_.bytesAllocated.fetch_add(size,
                        std::memory_order_relaxed);
                    if (cb_.onAllocate) cb_.onAllocate(size);
                    return result;
                }
            }
        }

        stats_.allocationFailures.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    // -------------------------------------------------------------------------
    // Old-gen allocation (promotion path)
    // -------------------------------------------------------------------------

    /**
     * @brief Allocate in old gen (called during minor GC promotion)
     */
    uintptr_t allocateOldGen(size_t size) {
        std::lock_guard<std::mutex> lock(oldGenMutex_);

        // Try current old-gen region
        if (currentOldGen_ && currentOldGen_->active) {
            std::lock_guard<std::mutex> rlock(currentOldGen_->mutex);
            uintptr_t result = currentOldGen_->allocateLocked(size);
            if (result != 0) {
                stats_.promotions.fetch_add(1, std::memory_order_relaxed);
                stats_.bytesPromoted.fetch_add(size,
                    std::memory_order_relaxed);
                return result;
            }
        }

        // Need new old-gen region
        if (!allocateNewOldGenRegion()) {
            return 0;
        }

        {
            std::lock_guard<std::mutex> rlock(currentOldGen_->mutex);
            uintptr_t result = currentOldGen_->allocateLocked(size);
            if (result != 0) {
                stats_.promotions.fetch_add(1, std::memory_order_relaxed);
                stats_.bytesPromoted.fetch_add(size,
                    std::memory_order_relaxed);
                return result;
            }
        }

        return 0;
    }

    // -------------------------------------------------------------------------
    // Large-object allocation
    // -------------------------------------------------------------------------

    /**
     * @brief Allocate a large object (gets its own region)
     */
    uintptr_t allocateLargeObject(size_t size) {
        if (!cb_.allocateRegion) return 0;

        // Calculate how many regions needed
        size_t regionsNeeded = (size + REGION_ALLOC_SIZE - 1) /
                               REGION_ALLOC_SIZE;

        if (regionsNeeded > config_.maxLargeObjectRegions) return 0;

        // For simplicity, only handle single-region large objects
        if (regionsNeeded > 1) {
            // Multi-region large objects would need contiguous regions
            // For now, fail if object > region size
            stats_.allocationFailures.fetch_add(1,
                std::memory_order_relaxed);
            return 0;
        }

        uint32_t regionIndex = 0;
        uintptr_t base = cb_.allocateRegion(regionIndex, 2); // 2 = LargeObject
        if (base == 0) {
            stats_.allocationFailures.fetch_add(1,
                std::memory_order_relaxed);
            return 0;
        }

        stats_.largeObjectAllocations.fetch_add(1,
            std::memory_order_relaxed);
        stats_.bytesAllocated.fetch_add(size, std::memory_order_relaxed);

        return base;
    }

    // -------------------------------------------------------------------------
    // Top-level allocator (dispatches based on size)
    // -------------------------------------------------------------------------

    /**
     * @brief Allocate an object of given size
     *
     * This is the main entry point for all allocations.
     */
    uintptr_t allocate(TLAB& tlab, size_t size) {
        if (size >= LARGE_OBJECT_THRESHOLD) {
            return allocateLargeObject(size);
        }
        return allocateNursery(tlab, size);
    }

    // -------------------------------------------------------------------------
    // Region management
    // -------------------------------------------------------------------------

    /**
     * @brief Release all Eden regions after minor GC
     */
    void releaseEdenRegions() {
        std::lock_guard<std::mutex> lock(edenMutex_);
        for (auto& region : edenRegions_) {
            if (cb_.releaseRegion) {
                cb_.releaseRegion(region->regionIndex);
            }
        }
        edenRegions_.clear();
        currentEden_ = nullptr;
    }

    /**
     * @brief Reset TLAB (called at GC safe-point)
     */
    void retireTLAB(TLAB& tlab) {
        // Account for unused TLAB space as waste
        tlab.reset();
    }

    const Stats& stats() const { return stats_; }

private:
    bool allocateNewEdenRegion() {
        if (!cb_.allocateRegion) return false;
        if (edenRegions_.size() >= config_.maxEdenRegions) return false;

        auto region = std::make_unique<RegionAllocState>();
        uint32_t regionIndex = 0;
        uintptr_t base = cb_.allocateRegion(regionIndex, 0); // 0 = Eden
        if (base == 0) return false;

        region->regionIndex = regionIndex;
        region->base = base;
        region->cursor = base;
        region->limit = base + REGION_ALLOC_SIZE;
        region->active = true;

        currentEden_ = region.get();
        edenRegions_.push_back(std::move(region));

        return true;
    }

    bool allocateNewOldGenRegion() {
        if (!cb_.allocateRegion) return false;
        if (oldGenRegions_.size() >= config_.maxOldGenRegions) return false;

        auto region = std::make_unique<RegionAllocState>();
        uint32_t regionIndex = 0;
        uintptr_t base = cb_.allocateRegion(regionIndex, 1); // 1 = OldGen
        if (base == 0) return false;

        region->regionIndex = regionIndex;
        region->base = base;
        region->cursor = base;
        region->limit = base + REGION_ALLOC_SIZE;
        region->active = true;

        currentOldGen_ = region.get();
        oldGenRegions_.push_back(std::move(region));

        return true;
    }

    size_t computeTLABSize(const TLAB& oldTlab) const {
        // Adaptive TLAB sizing based on previous usage
        if (!oldTlab.isValid()) return config_.tlabSize;

        size_t used = oldTlab.used();
        size_t cap = oldTlab.capacity();

        if (cap > 0 && used > cap * 3 / 4) {
            // Used most of it → grow
            return std::min(cap * 2, TLAB_MAX_SIZE);
        }
        if (cap > 0 && used < cap / 4) {
            // Wasted most of it → shrink
            return std::max(cap / 2, TLAB_MIN_SIZE);
        }

        return cap > 0 ? cap : config_.tlabSize;
    }

    Config config_;
    Callbacks cb_;
    Stats stats_;

    // Eden regions
    std::mutex edenMutex_;
    std::vector<std::unique_ptr<RegionAllocState>> edenRegions_;
    RegionAllocState* currentEden_ = nullptr;

    // Old-gen regions
    std::mutex oldGenMutex_;
    std::vector<std::unique_ptr<RegionAllocState>> oldGenRegions_;
    RegionAllocState* currentOldGen_ = nullptr;
};

} // namespace Zepra::Heap
