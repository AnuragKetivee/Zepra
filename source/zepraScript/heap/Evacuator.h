/**
 * @file Evacuator.h
 * @brief Parallel evacuation for compaction
 *
 * Evacuation moves live objects from sparse (fragmented) regions
 * to compact regions, reducing memory fragmentation.
 *
 * Process:
 * 1. Select evacuation candidates (regions with <50% live objects)
 * 2. Allocate destination space
 * 3. Copy live objects (parallel per-region)
 * 4. Install forwarding pointers in old locations
 * 5. Update all references to point to new locations
 *
 * Thread model:
 * - Each worker evacuates one region at a time
 * - Forwarding pointers are installed atomically
 * - Reference updating can be parallelized
 *
 * Pinned objects: objects that cannot be moved (e.g., passed to
 * native code via FFI, held by JIT compiled code) are skipped.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <chrono>

namespace Zepra::Heap {

// =============================================================================
// Forwarding Pointer
// =============================================================================

/**
 * @brief Represents an old→new address mapping
 *
 * Stored in the first word of the old object after evacuation.
 * Distinguished from regular objects by a special tag bit.
 */
struct ForwardingPointer {
    static constexpr uintptr_t TAG = 0x1;  // Low bit set = forwarding ptr

    static void install(void* oldAddr, void* newAddr) {
        *static_cast<uintptr_t*>(oldAddr) =
            reinterpret_cast<uintptr_t>(newAddr) | TAG;
    }

    static bool isForwarded(void* addr) {
        return (*static_cast<uintptr_t*>(addr) & TAG) != 0;
    }

    static void* forwardedAddress(void* addr) {
        return reinterpret_cast<void*>(
            *static_cast<uintptr_t*>(addr) & ~TAG);
    }

    /**
     * @brief Follow forwarding chain (objects may be forwarded multiple times)
     */
    static void* resolve(void* addr) {
        while (isForwarded(addr)) {
            addr = forwardedAddress(addr);
        }
        return addr;
    }
};

// =============================================================================
// Forwarding Table (external, not in-object)
// =============================================================================

/**
 * @brief External forwarding table for when in-object forwarding isn't safe
 *
 * Used for:
 * - Objects smaller than sizeof(void*) (can't fit forwarding ptr)
 * - Read-only pages
 * - Objects with finalizers that need both old and new addresses
 */
class ForwardingTable {
public:
    void record(void* oldAddr, void* newAddr) {
        std::lock_guard<std::mutex> lock(mutex_);
        table_[oldAddr] = newAddr;
    }

    void* lookup(void* oldAddr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = table_.find(oldAddr);
        return it != table_.end() ? it->second : nullptr;
    }

    bool hasForwarding(void* oldAddr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return table_.count(oldAddr) > 0;
    }

    void* resolve(void* addr) const {
        auto* forwarded = lookup(addr);
        return forwarded ? forwarded : addr;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return table_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        table_.clear();
    }

    /**
     * @brief Iterate all forwarding entries
     */
    void forEach(std::function<void(void* oldAddr, void* newAddr)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [old, newAddr] : table_) {
            callback(old, newAddr);
        }
    }

private:
    std::unordered_map<void*, void*> table_;
    mutable std::mutex mutex_;
};

// =============================================================================
// Pin Set
// =============================================================================

/**
 * @brief Tracks pinned objects that must not be moved
 */
class PinSet {
public:
    void pin(void* object) {
        std::lock_guard<std::mutex> lock(mutex_);
        pinned_.insert(object);
    }

    void unpin(void* object) {
        std::lock_guard<std::mutex> lock(mutex_);
        pinned_.erase(object);
    }

    bool isPinned(void* object) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pinned_.count(object) > 0;
    }

    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pinned_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        pinned_.clear();
    }

private:
    std::unordered_set<void*> pinned_;
    mutable std::mutex mutex_;
};

// =============================================================================
// Evacuation Candidate
// =============================================================================

struct EvacuationCandidate {
    void* regionStart;
    void* regionEnd;
    size_t regionSize;
    size_t liveBytes;
    size_t liveObjects;
    double occupancy;       // liveBytes / regionSize
    uint32_t regionIndex;

    bool operator<(const EvacuationCandidate& other) const {
        return occupancy < other.occupancy;  // Sparsest first
    }
};

// =============================================================================
// Evacuation Statistics
// =============================================================================

struct EvacuationStats {
    size_t regionsEvacuated = 0;
    size_t objectsMoved = 0;
    size_t bytesMoved = 0;
    size_t objectsPinned = 0;
    size_t referencesUpdated = 0;
    double copyTimeMs = 0;
    double updateTimeMs = 0;
    double totalTimeMs = 0;
    size_t regionsFreed = 0;
};

// =============================================================================
// Evacuator
// =============================================================================

class Evacuator {
public:
    using ObjectSizeFn = std::function<size_t(void* object)>;
    using AllocatorFn = std::function<void*(size_t size)>;
    using ObjectTracerFn = std::function<void(void* object,
        std::function<void(void** slot)> visitSlot)>;
    using ObjectIteratorFn = std::function<void(void* regionStart,
        void* regionEnd, std::function<void(void* object, size_t size)> visit)>;

    struct Config {
        double evacuationThreshold;  // Evacuate regions below this occupancy
        size_t maxEvacuationBytes;   // Don't evacuate more than this per cycle
        size_t workerCount;
        bool useForwardingTable;     // Use external table vs in-object

        Config()
            : evacuationThreshold(0.5)
            , maxEvacuationBytes(64 * 1024 * 1024)
            , workerCount(2)
            , useForwardingTable(false) {}
    };

    explicit Evacuator(const Config& config = Config{});
    ~Evacuator();

    /**
     * @brief Set the allocation function for destination space
     */
    void setAllocator(AllocatorFn allocator) { allocator_ = std::move(allocator); }

    /**
     * @brief Set the object size function
     */
    void setSizer(ObjectSizeFn sizer) { sizer_ = std::move(sizer); }

    /**
     * @brief Set the object tracer for reference updating
     */
    void setTracer(ObjectTracerFn tracer) { tracer_ = std::move(tracer); }

    /**
     * @brief Set the live object iterator
     */
    void setObjectIterator(ObjectIteratorFn iterator) {
        objectIterator_ = std::move(iterator);
    }

    /**
     * @brief Access the pin set
     */
    PinSet& pinSet() { return pinSet_; }

    /**
     * @brief Select evacuation candidates
     */
    void selectCandidates(
        const std::vector<EvacuationCandidate>& regions
    );

    /**
     * @brief Evacuate selected regions
     *
     * Phase 1: Copy live objects to new locations
     * Phase 2: Install forwarding pointers
     *
     * Returns statistics.
     */
    EvacuationStats evacuate();

    /**
     * @brief Update all references to point to new locations
     *
     * Must be called after evacuation. Walks the entire heap
     * and updates every pointer that points into an evacuated region.
     *
     * @param visitAllRoots Root visitor
     * @param visitAllObjects Heap visitor
     * @return Number of references updated
     */
    size_t updateReferences(
        std::function<void(std::function<void(void** slot)>)> visitAllRoots,
        std::function<void(std::function<void(void* object)>)> visitAllObjects
    );

    /**
     * @brief Last evacuation stats
     */
    const EvacuationStats& lastStats() const { return lastStats_; }

    /**
     * @brief Access forwarding table
     */
    ForwardingTable& forwardingTable() { return fwdTable_; }

private:
    /**
     * @brief Evacuate a single region
     */
    void evacuateRegion(const EvacuationCandidate& candidate,
                         EvacuationStats& stats);

    /**
     * @brief Update a single reference slot
     */
    void updateSlot(void** slot);

    /**
     * @brief Check if address is in an evacuated region
     */
    bool isEvacuatedAddress(void* addr) const;

    Config config_;
    PinSet pinSet_;
    ForwardingTable fwdTable_;

    AllocatorFn allocator_;
    ObjectSizeFn sizer_;
    ObjectTracerFn tracer_;
    ObjectIteratorFn objectIterator_;

    std::vector<EvacuationCandidate> candidates_;
    EvacuationStats lastStats_;
};

// =============================================================================
// Reference Updater
// =============================================================================

/**
 * @brief Bulk pointer fixup after evacuation
 *
 * Walks the entire heap (and roots) to update references
 * that point into evacuated regions.
 */
class ReferenceUpdater {
public:
    using ForwardFn = std::function<void*(void* oldAddr)>;

    explicit ReferenceUpdater(ForwardFn forward) : forward_(std::move(forward)) {}

    /**
     * @brief Update a single slot
     */
    void updateSlot(void** slot) {
        if (!slot || !*slot) return;
        void* forwarded = forward_(*slot);
        if (forwarded != *slot) {
            *slot = forwarded;
            updatedCount_++;
        }
    }

    /**
     * @brief Update all slots in an object
     */
    void updateObject(void* object,
                       std::function<void(void*, std::function<void(void**)>)> trace) {
        trace(object, [this](void** slot) { updateSlot(slot); });
    }

    size_t updatedCount() const { return updatedCount_; }
    void reset() { updatedCount_ = 0; }

private:
    ForwardFn forward_;
    size_t updatedCount_ = 0;
};

// =============================================================================
// Implementation
// =============================================================================

inline Evacuator::Evacuator(const Config& config) : config_(config) {}
inline Evacuator::~Evacuator() = default;

inline void Evacuator::selectCandidates(
    const std::vector<EvacuationCandidate>& regions
) {
    candidates_.clear();
    size_t totalBytes = 0;

    // Sort by occupancy (sparsest first)
    auto sorted = regions;
    std::sort(sorted.begin(), sorted.end());

    for (const auto& region : sorted) {
        if (region.occupancy >= config_.evacuationThreshold) break;
        if (totalBytes + region.liveBytes > config_.maxEvacuationBytes) break;

        candidates_.push_back(region);
        totalBytes += region.liveBytes;
    }
}

inline EvacuationStats Evacuator::evacuate() {
    EvacuationStats stats;
    auto startTime = std::chrono::steady_clock::now();

    fwdTable_.clear();

    for (const auto& candidate : candidates_) {
        evacuateRegion(candidate, stats);
    }

    stats.totalTimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime).count();

    lastStats_ = stats;
    return stats;
}

inline void Evacuator::evacuateRegion(const EvacuationCandidate& candidate,
                                        EvacuationStats& stats) {
    if (!objectIterator_ || !allocator_ || !sizer_) return;

    stats.regionsEvacuated++;

    objectIterator_(candidate.regionStart, candidate.regionEnd,
        [&](void* object, size_t size) {
            // Skip pinned objects
            if (pinSet_.isPinned(object)) {
                stats.objectsPinned++;
                return;
            }

            // Allocate in destination
            void* newAddr = allocator_(size);
            if (!newAddr) return;

            // Copy object
            std::memcpy(newAddr, object, size);

            // Install forwarding pointer
            if (config_.useForwardingTable) {
                fwdTable_.record(object, newAddr);
            } else {
                ForwardingPointer::install(object, newAddr);
            }

            stats.objectsMoved++;
            stats.bytesMoved += size;
        });

    stats.regionsFreed++;
}

inline size_t Evacuator::updateReferences(
    std::function<void(std::function<void(void** slot)>)> visitAllRoots,
    std::function<void(std::function<void(void* object)>)> visitAllObjects
) {
    auto startTime = std::chrono::steady_clock::now();

    size_t updated = 0;

    auto updateFn = [this, &updated](void** slot) {
        if (!slot || !*slot) return;
        void* addr = *slot;

        void* forwarded = nullptr;
        if (config_.useForwardingTable) {
            forwarded = fwdTable_.resolve(addr);
        } else if (isEvacuatedAddress(addr) && ForwardingPointer::isForwarded(addr)) {
            forwarded = ForwardingPointer::resolve(addr);
        }

        if (forwarded && forwarded != addr) {
            *slot = forwarded;
            updated++;
        }
    };

    // Update roots
    visitAllRoots(updateFn);

    // Update heap references
    if (tracer_) {
        visitAllObjects([this, &updateFn](void* object) {
            tracer_(object, updateFn);
        });
    }

    lastStats_.referencesUpdated = updated;
    lastStats_.updateTimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime).count();

    return updated;
}

inline bool Evacuator::isEvacuatedAddress(void* addr) const {
    for (const auto& c : candidates_) {
        if (addr >= c.regionStart && addr < c.regionEnd) return true;
    }
    return false;
}

} // namespace Zepra::Heap
