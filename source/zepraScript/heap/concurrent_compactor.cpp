/**
 * @file concurrent_compactor.cpp
 * @brief Concurrent and parallel compaction
 *
 * Unlike the stop-the-world HeapCompactor (heap_compactor.cpp),
 * this compactor runs concurrently with the mutator for most of
 * its work, requiring only brief pauses for reference fixup.
 *
 * Phases:
 * 1. [Concurrent] Select evacuation candidates (sparse regions)
 * 2. [Concurrent] Copy live objects from source to destination regions
 *    - Uses compare-and-swap to install forwarding pointers
 *    - Mutator reads check forwarding (read barrier)
 * 3. [STW pause] Fix all roots (stack, globals, handles)
 * 4. [Concurrent] Fix heap references (via card table scan)
 * 5. [Concurrent] Release evacuated regions
 *
 * Forwarding scheme (Brooks pointer):
 * - Each object header has a forwarding word
 * - Initially points to self (identity)
 * - During compaction, CAS'd to point to the copy
 * - Read barrier follows the forwarding pointer
 * - After fixup, forwarding pointers are removed
 *
 * This design is inspired by Shenandoah GC's forwarding approach.
 */

#include <atomic>
#include <mutex>
#include <thread>
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
#include <unordered_map>

namespace Zepra::Heap {

// =============================================================================
// Forwarding Pointer (Brooks Pointer)
// =============================================================================

/**
 * @brief Manages per-object forwarding pointers
 *
 * Each object's header word is used as a forwarding pointer:
 * - Not forwarded: points to self
 * - Forwarded: points to new location
 *
 * The read barrier dereferences through this pointer.
 */
class BrooksForwarding {
public:
    /**
     * @brief Initialize an object's forwarding pointer to self
     */
    static void initSelf(uintptr_t objAddr) {
        auto* fwd = reinterpret_cast<std::atomic<uintptr_t>*>(objAddr);
        fwd->store(objAddr, std::memory_order_release);
    }

    /**
     * @brief Try to install a forwarding pointer (CAS)
     * @return true if we won the race (installed our copy)
     */
    static bool tryForward(uintptr_t objAddr, uintptr_t newAddr) {
        auto* fwd = reinterpret_cast<std::atomic<uintptr_t>*>(objAddr);
        uintptr_t expected = objAddr;  // Self-referencing = not forwarded
        return fwd->compare_exchange_strong(expected, newAddr,
            std::memory_order_acq_rel, std::memory_order_acquire);
    }

    /**
     * @brief Read the forwarding pointer (read barrier)
     */
    static uintptr_t resolve(uintptr_t objAddr) {
        auto* fwd = reinterpret_cast<std::atomic<uintptr_t>*>(objAddr);
        return fwd->load(std::memory_order_acquire);
    }

    /**
     * @brief Check if object is forwarded
     */
    static bool isForwarded(uintptr_t objAddr) {
        return resolve(objAddr) != objAddr;
    }
};

// =============================================================================
// Evacuation Region
// =============================================================================

struct EvacuationRegion {
    uint32_t regionIndex;
    uintptr_t baseAddr;
    size_t liveBytes;
    double occupancy;
    bool evacuated;     // All live objects copied out
    bool fixedUp;       // All references updated
};

// =============================================================================
// Copy Task
// =============================================================================

struct CopyTask {
    uintptr_t srcAddr;
    size_t objSize;
    uint32_t srcRegion;
};

// =============================================================================
// Concurrent Compactor
// =============================================================================

class ConcurrentCompactor {
public:
    struct Config {
        double evacuateThreshold;   // Regions below this occupancy
        size_t maxEvacuateRegions;
        size_t copyWorkers;         // Parallel copy threads
        size_t fixupWorkers;        // Parallel fixup threads

        Config()
            : evacuateThreshold(0.50)
            , maxEvacuateRegions(64)
            , copyWorkers(2)
            , fixupWorkers(2) {}
    };

    struct Callbacks {
        // Get region info
        std::function<std::vector<EvacuationRegion>()> getRegions;

        // Get mark bitmap for a region
        std::function<const uint8_t*(uint32_t regionIndex)> getMarkBitmap;

        // Get object size
        std::function<size_t(uintptr_t objAddr)> objectSize;

        // Allocate in a non-evacuated region
        std::function<uintptr_t(size_t size, uint32_t avoidRegion)> allocate;

        // Enumerate roots for fixup
        std::function<void(std::function<void(void** slot)>)> enumerateRoots;

        // Trace all references of an object
        std::function<void(uintptr_t obj, std::function<void(void** slot)>)>
            traceObject;

        // Iterate dirty cards for concurrent fixup
        std::function<void(std::function<void(uintptr_t cardBase,
                                                size_t cardSize)>)>
            iterateDirtyCards;

        // Release a region
        std::function<void(uint32_t regionIndex)> releaseRegion;
    };

    struct Stats {
        size_t regionsEvaluated;
        size_t regionsEvacuated;
        size_t objectsCopied;
        size_t bytesCopied;
        size_t forwardingCASWins;
        size_t forwardingCASLosses;
        size_t referencesFixed;
        size_t regionsReleased;
        double selectMs;
        double copyMs;
        double fixupMs;
        double releaseMs;
        double totalMs;
    };

    explicit ConcurrentCompactor(const Config& config = Config{})
        : config_(config) {}

    void setCallbacks(Callbacks cb) { cb_ = std::move(cb); }

    /**
     * @brief Phase 1: Select evacuation candidates [concurrent]
     */
    std::vector<EvacuationRegion> selectCandidates() {
        if (!cb_.getRegions) return {};

        auto regions = cb_.getRegions();
        stats_.regionsEvaluated = regions.size();

        // Sort by occupancy ascending
        std::sort(regions.begin(), regions.end(),
            [](const EvacuationRegion& a, const EvacuationRegion& b) {
                return a.occupancy < b.occupancy;
            });

        std::vector<EvacuationRegion> candidates;
        for (auto& region : regions) {
            if (region.occupancy >= config_.evacuateThreshold) break;
            if (region.liveBytes == 0) {
                // Empty — just release directly
                region.evacuated = true;
                candidates.push_back(region);
                continue;
            }
            if (candidates.size() >= config_.maxEvacuateRegions) break;
            candidates.push_back(region);
        }

        return candidates;
    }

    /**
     * @brief Phase 2: Copy live objects [concurrent, parallel]
     *
     * Multiple threads copy objects from evacuation candidates.
     * Uses CAS on forwarding pointers to handle races.
     */
    void copyPhase(std::vector<EvacuationRegion>& candidates) {
        if (!cb_.getMarkBitmap || !cb_.objectSize || !cb_.allocate) return;

        auto copyStart = std::chrono::steady_clock::now();

        // Build work list: all live objects in candidate regions
        std::vector<CopyTask> tasks;
        for (auto& cand : candidates) {
            if (cand.liveBytes == 0) continue;

            const uint8_t* bitmap = cb_.getMarkBitmap(cand.regionIndex);
            if (!bitmap) continue;

            // Walk bitmap to find live objects
            size_t cellCount = 256 * 1024 / 8;  // 256KB / 8-byte cells
            size_t bitmapBytes = cellCount / 8;

            for (size_t byteIdx = 0; byteIdx < bitmapBytes; byteIdx++) {
                uint8_t byte = bitmap[byteIdx];
                if (byte == 0) continue;

                for (int bit = 0; bit < 8; bit++) {
                    if (byte & (1 << bit)) {
                        size_t cellIdx = byteIdx * 8 + bit;
                        uintptr_t objAddr = cand.baseAddr + cellIdx * 8;

                        CopyTask task;
                        task.srcAddr = objAddr;
                        task.objSize = cb_.objectSize(objAddr);
                        task.srcRegion = cand.regionIndex;
                        if (task.objSize > 0) {
                            tasks.push_back(task);
                        }
                    }
                }
            }
        }

        // Dispatch copy tasks across worker threads
        std::atomic<size_t> taskIndex{0};
        std::atomic<size_t> totalCopied{0};
        std::atomic<size_t> totalBytes{0};
        std::atomic<size_t> casWins{0};
        std::atomic<size_t> casLosses{0};

        auto worker = [&]() {
            while (true) {
                size_t idx = taskIndex.fetch_add(1, std::memory_order_relaxed);
                if (idx >= tasks.size()) break;

                const auto& task = tasks[idx];

                // Allocate in a non-evacuated region
                uintptr_t newAddr = cb_.allocate(
                    task.objSize, task.srcRegion);
                if (newAddr == 0) continue;

                // Copy the object
                std::memcpy(reinterpret_cast<void*>(newAddr),
                            reinterpret_cast<const void*>(task.srcAddr),
                            task.objSize);

                // Initialize forwarding on the copy (points to self)
                BrooksForwarding::initSelf(newAddr);

                // CAS the forwarding pointer on the original
                if (BrooksForwarding::tryForward(task.srcAddr, newAddr)) {
                    casWins.fetch_add(1, std::memory_order_relaxed);
                    totalCopied.fetch_add(1, std::memory_order_relaxed);
                    totalBytes.fetch_add(task.objSize,
                                          std::memory_order_relaxed);
                } else {
                    // Another thread beat us; our copy is wasted
                    // (in a real impl, we'd return the allocation)
                    casLosses.fetch_add(1, std::memory_order_relaxed);
                }
            }
        };

        size_t numWorkers = std::min(config_.copyWorkers, tasks.size());
        if (numWorkers == 0) numWorkers = 1;

        std::vector<std::thread> threads;
        for (size_t t = 0; t < numWorkers; t++) {
            threads.emplace_back(worker);
        }
        for (auto& t : threads) t.join();

        stats_.objectsCopied = totalCopied.load();
        stats_.bytesCopied = totalBytes.load();
        stats_.forwardingCASWins = casWins.load();
        stats_.forwardingCASLosses = casLosses.load();

        // Mark regions as evacuated
        for (auto& cand : candidates) {
            cand.evacuated = true;
        }

        stats_.copyMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - copyStart).count();
    }

    /**
     * @brief Phase 3: Fix roots [STW pause — brief]
     *
     * Must be done in a stop-the-world pause to ensure
     * consistency of root set.
     */
    void fixRoots() {
        if (!cb_.enumerateRoots) return;

        cb_.enumerateRoots([this](void** slot) {
            if (!slot || !*slot) return;
            auto addr = reinterpret_cast<uintptr_t>(*slot);
            uintptr_t newAddr = BrooksForwarding::resolve(addr);
            if (newAddr != addr) {
                *slot = reinterpret_cast<void*>(newAddr);
                stats_.referencesFixed++;
            }
        });
    }

    /**
     * @brief Phase 4: Fix heap references [concurrent]
     *
     * Scans non-evacuated regions for references to evacuated
     * objects and updates them via forwarding pointers.
     */
    void fixHeapReferences() {
        if (!cb_.iterateDirtyCards) return;

        auto fixStart = std::chrono::steady_clock::now();

        cb_.iterateDirtyCards([this](uintptr_t cardBase, size_t cardSize) {
            // Scan all pointer-sized slots in this card
            for (uintptr_t addr = cardBase; addr + sizeof(void*) <= cardBase + cardSize;
                 addr += sizeof(void*)) {
                auto* slot = reinterpret_cast<void**>(addr);
                if (!*slot) continue;

                auto objAddr = reinterpret_cast<uintptr_t>(*slot);
                if (BrooksForwarding::isForwarded(objAddr)) {
                    uintptr_t newAddr = BrooksForwarding::resolve(objAddr);
                    *slot = reinterpret_cast<void*>(newAddr);
                    stats_.referencesFixed++;
                }
            }
        });

        stats_.fixupMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - fixStart).count();
    }

    /**
     * @brief Phase 5: Release evacuated regions [concurrent]
     */
    void releaseEvacuated(const std::vector<EvacuationRegion>& candidates) {
        auto relStart = std::chrono::steady_clock::now();

        for (const auto& cand : candidates) {
            if (cand.evacuated && cb_.releaseRegion) {
                cb_.releaseRegion(cand.regionIndex);
                stats_.regionsReleased++;
            }
        }

        stats_.releaseMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - relStart).count();
    }

    /**
     * @brief Run full compaction cycle
     */
    Stats compact() {
        stats_ = {};
        auto start = std::chrono::steady_clock::now();

        // Phase 1: Select
        auto selectStart = std::chrono::steady_clock::now();
        auto candidates = selectCandidates();
        stats_.selectMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - selectStart).count();

        if (candidates.empty()) {
            stats_.totalMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            return stats_;
        }

        stats_.regionsEvacuated = candidates.size();

        // Phase 2: Copy [concurrent]
        copyPhase(candidates);

        // Phase 3: Fix roots [STW — the only pause]
        fixRoots();

        // Phase 4: Fix heap references [concurrent]
        fixHeapReferences();

        // Phase 5: Release [concurrent]
        releaseEvacuated(candidates);

        stats_.totalMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        return stats_;
    }

    const Stats& lastStats() const { return stats_; }

private:
    Config config_;
    Callbacks cb_;
    Stats stats_{};
};

} // namespace Zepra::Heap
