// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file incremental_compactor.cpp
 * @brief Incremental heap compaction engine
 *
 * Unlike full-stop compaction, incremental compaction moves objects
 * across multiple GC cycles, spreading the pause cost:
 *
 * 1. Selection: pick sparse pages (< threshold occupancy)
 * 2. Planning: compute per-page forwarding maps
 * 3. Copying: copy N objects per incremental step (time-budgeted)
 * 4. Fixup: update references pointing to moved objects
 * 5. Release: return empty pages to OS
 *
 * Integration with concurrent marking:
 * - Compaction runs after marking completes
 * - SATB barrier handles references to objects being moved
 * - Forwarding pointers are installed atomically
 *
 * Page classification:
 * - FULL: > 90% live — leave alone
 * - PARTIAL: 50-90% live — considered but low priority
 * - SPARSE: < 50% live — high-priority evacuation candidate
 * - EMPTY: 0% live — immediately release
 */

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <deque>
#include <queue>
#include <functional>
#include <chrono>
#include <cstring>
#include <cassert>
#include <memory>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace Zepra::Heap {

// =============================================================================
// Page Classification
// =============================================================================

enum class PageClass : uint8_t {
    Empty,      // 0% live
    Sparse,     // < 50% live
    Partial,    // 50-90% live
    Full,       // > 90% live
    Pinned,     // Contains pinned objects
    Code,       // Executable code page
};

static const char* pageClassName(PageClass c) {
    switch (c) {
        case PageClass::Empty: return "Empty";
        case PageClass::Sparse: return "Sparse";
        case PageClass::Partial: return "Partial";
        case PageClass::Full: return "Full";
        case PageClass::Pinned: return "Pinned";
        case PageClass::Code: return "Code";
        default: return "Unknown";
    }
}

// =============================================================================
// Page Metadata
// =============================================================================

struct PageMetadata {
    void* baseAddr;
    size_t pageSize;
    uint32_t pageIndex;

    // Liveness data (from marking)
    size_t liveBytes;
    size_t liveObjects;
    size_t totalBytes;
    double occupancy;
    PageClass classification;

    // Compaction state
    enum class CompactState : uint8_t {
        Idle,
        Selected,       // Chosen for compaction
        Copying,        // Objects being copied out
        Forwarding,     // Forwarding pointers installed
        Released,       // Page released
    };
    CompactState compactState;

    // History
    uint32_t gcsSinceCompaction;
    uint32_t timesCompacted;

    void classify() {
        if (liveBytes == 0) {
            classification = PageClass::Empty;
        } else if (totalBytes > 0) {
            occupancy = static_cast<double>(liveBytes) /
                        static_cast<double>(totalBytes);
            if (occupancy < 0.5) classification = PageClass::Sparse;
            else if (occupancy < 0.9) classification = PageClass::Partial;
            else classification = PageClass::Full;
        }
    }

    // Priority score (lower = compact first)
    double compactionPriority() const {
        if (classification == PageClass::Empty) return 0.0;
        if (classification == PageClass::Pinned) return 999.0;
        if (classification == PageClass::Full) return 100.0;
        // Sparse pages with few live bytes are best candidates
        return occupancy * 10.0 + static_cast<double>(liveBytes) / 1024.0;
    }
};

// =============================================================================
// Compaction Plan
// =============================================================================

struct CompactionPlan {
    struct MoveEntry {
        void* sourceAddr;       // Object in source page
        void* destAddr;         // Allocated slot in destination page
        size_t objectSize;
        uint32_t sourcePageIdx;
        uint32_t destPageIdx;
    };

    std::vector<uint32_t> sourcePages;          // Pages to evacuate
    std::vector<uint32_t> destPages;            // Pages to receive objects
    std::vector<MoveEntry> moves;               // Individual object moves
    size_t totalBytesToMove = 0;
    size_t pagesToRelease = 0;
    double estimatedPauseMs = 0;

    void clear() {
        sourcePages.clear();
        destPages.clear();
        moves.clear();
        totalBytesToMove = 0;
        pagesToRelease = 0;
        estimatedPauseMs = 0;
    }

    bool isEmpty() const { return moves.empty(); }
};

// =============================================================================
// Compaction Statistics
// =============================================================================

struct CompactionStats {
    // Per-cycle stats
    size_t pagesEvaluated = 0;
    size_t pagesSelected = 0;
    size_t pagesReleased = 0;
    size_t objectsMoved = 0;
    size_t bytesMoved = 0;
    size_t referencesUpdated = 0;
    double planningMs = 0;
    double copyingMs = 0;
    double fixupMs = 0;
    double releaseMs = 0;
    double totalMs = 0;

    // Cumulative stats
    uint64_t totalCycles = 0;
    uint64_t totalPagesMoved = 0;
    uint64_t totalBytesCompacted = 0;
    uint64_t totalPagesReleased = 0;
    double totalTimeMs = 0;

    // Fragmentation
    double fragmentationBefore = 0;
    double fragmentationAfter = 0;
    double fragmentationReduction = 0;
};

// =============================================================================
// Incremental Compactor
// =============================================================================

class IncrementalCompactor {
public:
    struct Config {
        double sparseThreshold;         // Pages below this occupancy are candidates
        size_t maxBytesPerStep;         // Max bytes to move per incremental step
        size_t maxPagesPerCycle;        // Max source pages per full cycle
        double stepBudgetMs;            // Time budget per incremental step
        size_t minFragmentationToCompact; // Don't compact if < this many sparse pages
        bool enableParallelCopy;
        size_t copyWorkers;

        Config()
            : sparseThreshold(0.5)
            , maxBytesPerStep(256 * 1024)
            , maxPagesPerCycle(16)
            , stepBudgetMs(1.0)
            , minFragmentationToCompact(4)
            , enableParallelCopy(true)
            , copyWorkers(2) {}
    };

    // VM interaction callbacks
    struct Callbacks {
        // Get page metadata for all pages
        std::function<std::vector<PageMetadata>()> getPageMetadata;

        // Allocate in a destination page
        std::function<void*(size_t size, uint32_t destPageIdx)> allocateInPage;

        // Iterate live objects in a page
        std::function<void(uint32_t pageIdx,
            std::function<void(void* obj, size_t size)>)> iteratePageObjects;

        // Trace object references (for fixup)
        std::function<void(void* obj,
            std::function<void(void** slot)>)> traceObject;

        // Enumerate roots (for fixup)
        std::function<void(std::function<void(void** slot)>)> enumerateRoots;

        // Release a page back to OS
        std::function<void(uint32_t pageIdx)> releasePage;

        // Check if object is pinned
        std::function<bool(void* obj)> isPinned;
    };

    explicit IncrementalCompactor(const Config& config = Config{});
    ~IncrementalCompactor();

    void setCallbacks(Callbacks callbacks) { cb_ = std::move(callbacks); }

    // -------------------------------------------------------------------------
    // Full compaction cycle
    // -------------------------------------------------------------------------

    /**
     * @brief Plan a compaction cycle
     * Evaluates pages, selects candidates, builds move list.
     * @return true if compaction is needed
     */
    bool planCompaction();

    /**
     * @brief Execute one incremental compaction step
     * Moves a batch of objects within the time budget.
     * @return true if compaction is complete
     */
    bool executeStep();

    /**
     * @brief Update all references to moved objects
     * Must be called after all copying steps complete.
     */
    void fixupReferences();

    /**
     * @brief Release empty pages
     */
    void releasePages();

    /**
     * @brief Run full compaction (plan + copy + fixup + release)
     */
    CompactionStats runFullCompaction();

    // -------------------------------------------------------------------------
    // State queries
    // -------------------------------------------------------------------------

    bool isCompacting() const { return compacting_; }
    const CompactionPlan& currentPlan() const { return plan_; }
    const CompactionStats& lastStats() const { return lastStats_; }

    /**
     * @brief Should we compact? (heuristic)
     */
    bool shouldCompact() const;

    /**
     * @brief Estimate fragmentation ratio
     */
    double estimateFragmentation() const;

private:
    void evaluatePages(CompactionStats& stats);
    void selectCandidates(CompactionStats& stats);
    void buildMoveList(CompactionStats& stats);

    Config config_;
    Callbacks cb_;
    CompactionPlan plan_;
    CompactionStats lastStats_;

    // Forwarding map: old address → new address
    std::unordered_map<void*, void*> forwardingMap_;

    bool compacting_ = false;
    size_t movesCursor_ = 0;        // Current position in plan_.moves

    // Page metadata cache
    std::vector<PageMetadata> pages_;
};

// =============================================================================
// Implementation
// =============================================================================

inline IncrementalCompactor::IncrementalCompactor(const Config& config)
    : config_(config) {}

inline IncrementalCompactor::~IncrementalCompactor() = default;

inline bool IncrementalCompactor::planCompaction() {
    CompactionStats stats;
    auto start = std::chrono::steady_clock::now();

    plan_.clear();
    forwardingMap_.clear();
    movesCursor_ = 0;

    evaluatePages(stats);
    selectCandidates(stats);

    if (plan_.sourcePages.size() < config_.minFragmentationToCompact) {
        return false;  // Not worth compacting
    }

    buildMoveList(stats);

    stats.planningMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();

    compacting_ = !plan_.isEmpty();
    return compacting_;
}

inline bool IncrementalCompactor::executeStep() {
    if (!compacting_ || plan_.isEmpty()) return true;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::microseconds(
                        static_cast<int64_t>(config_.stepBudgetMs * 1000));

    size_t bytesThisStep = 0;

    while (movesCursor_ < plan_.moves.size()) {
        const auto& move = plan_.moves[movesCursor_];

        // Skip pinned objects
        if (cb_.isPinned && cb_.isPinned(move.sourceAddr)) {
            movesCursor_++;
            lastStats_.objectsMoved++;  // Count but don't move
            continue;
        }

        // Copy object
        std::memcpy(move.destAddr, move.sourceAddr, move.objectSize);

        // Record forwarding
        forwardingMap_[move.sourceAddr] = move.destAddr;

        lastStats_.objectsMoved++;
        lastStats_.bytesMoved += move.objectSize;
        bytesThisStep += move.objectSize;

        movesCursor_++;

        // Budget checks
        if (bytesThisStep >= config_.maxBytesPerStep) break;
        if (std::chrono::steady_clock::now() >= deadline) break;
    }

    return movesCursor_ >= plan_.moves.size();
}

inline void IncrementalCompactor::fixupReferences() {
    auto start = std::chrono::steady_clock::now();

    auto updateSlot = [this](void** slot) {
        if (!slot || !*slot) return;
        auto it = forwardingMap_.find(*slot);
        if (it != forwardingMap_.end()) {
            *slot = it->second;
            lastStats_.referencesUpdated++;
        }
    };

    // Fix roots
    if (cb_.enumerateRoots) {
        cb_.enumerateRoots(updateSlot);
    }

    // Fix heap references — iterate all non-evacuated pages
    for (const auto& page : pages_) {
        if (page.compactState == PageMetadata::CompactState::Idle ||
            page.compactState == PageMetadata::CompactState::Released) {
            if (cb_.iteratePageObjects) {
                cb_.iteratePageObjects(page.pageIndex,
                    [this, &updateSlot](void* obj, size_t /*size*/) {
                        if (cb_.traceObject) {
                            cb_.traceObject(obj, updateSlot);
                        }
                    });
            }
        }
    }

    lastStats_.fixupMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

inline void IncrementalCompactor::releasePages() {
    auto start = std::chrono::steady_clock::now();

    for (uint32_t pageIdx : plan_.sourcePages) {
        if (cb_.releasePage) {
            cb_.releasePage(pageIdx);
            lastStats_.pagesReleased++;
        }
    }

    lastStats_.releaseMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();

    compacting_ = false;
}

inline CompactionStats IncrementalCompactor::runFullCompaction() {
    lastStats_ = {};
    auto startTime = std::chrono::steady_clock::now();

    if (!planCompaction()) {
        return lastStats_;
    }

    // Copy all objects
    while (!executeStep()) {}

    // Fix all references
    fixupReferences();

    // Release evacuated pages
    releasePages();

    lastStats_.totalMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime).count();
    lastStats_.totalCycles++;

    return lastStats_;
}

inline bool IncrementalCompactor::shouldCompact() const {
    size_t sparseCount = 0;
    for (const auto& page : pages_) {
        if (page.classification == PageClass::Sparse) sparseCount++;
    }
    return sparseCount >= config_.minFragmentationToCompact;
}

inline double IncrementalCompactor::estimateFragmentation() const {
    if (pages_.empty()) return 0;
    size_t totalLive = 0;
    size_t totalCapacity = 0;
    for (const auto& page : pages_) {
        totalLive += page.liveBytes;
        totalCapacity += page.totalBytes;
    }
    if (totalCapacity == 0) return 0;
    return 1.0 - static_cast<double>(totalLive) /
                  static_cast<double>(totalCapacity);
}

inline void IncrementalCompactor::evaluatePages(CompactionStats& stats) {
    if (!cb_.getPageMetadata) return;
    pages_ = cb_.getPageMetadata();

    for (auto& page : pages_) {
        page.classify();
        stats.pagesEvaluated++;
    }
}

inline void IncrementalCompactor::selectCandidates(CompactionStats& stats) {
    // Sort by compaction priority
    std::vector<size_t> indices(pages_.size());
    std::iota(indices.begin(), indices.end(), 0);

    std::sort(indices.begin(), indices.end(),
        [this](size_t a, size_t b) {
            return pages_[a].compactionPriority() <
                   pages_[b].compactionPriority();
        });

    // Select source pages (sparse/empty) and dest pages (partial/full with space)
    for (size_t idx : indices) {
        auto& page = pages_[idx];

        if (page.classification == PageClass::Empty) {
            plan_.sourcePages.push_back(page.pageIndex);
            plan_.pagesToRelease++;
            stats.pagesSelected++;
        } else if (page.classification == PageClass::Sparse) {
            if (plan_.sourcePages.size() < config_.maxPagesPerCycle) {
                plan_.sourcePages.push_back(page.pageIndex);
                plan_.totalBytesToMove += page.liveBytes;
                stats.pagesSelected++;
            }
        } else if (page.classification == PageClass::Partial ||
                   page.classification == PageClass::Full) {
            // Potential destination page
            plan_.destPages.push_back(page.pageIndex);
        }
    }
}

inline void IncrementalCompactor::buildMoveList(CompactionStats& stats) {
    if (plan_.sourcePages.empty() || plan_.destPages.empty()) return;

    size_t destIdx = 0;

    for (uint32_t srcPageIdx : plan_.sourcePages) {
        auto& srcPage = pages_[srcPageIdx];
        if (srcPage.classification == PageClass::Empty) continue;

        if (!cb_.iteratePageObjects) continue;

        cb_.iteratePageObjects(srcPageIdx,
            [&](void* obj, size_t size) {
                // Find destination space
                void* destAddr = nullptr;
                while (destIdx < plan_.destPages.size()) {
                    if (cb_.allocateInPage) {
                        destAddr = cb_.allocateInPage(size, plan_.destPages[destIdx]);
                    }
                    if (destAddr) break;
                    destIdx++;
                }

                if (destAddr) {
                    CompactionPlan::MoveEntry move;
                    move.sourceAddr = obj;
                    move.destAddr = destAddr;
                    move.objectSize = size;
                    move.sourcePageIdx = srcPageIdx;
                    move.destPageIdx = plan_.destPages[destIdx];
                    plan_.moves.push_back(move);
                }
            });
    }

    (void)stats;
}

// =============================================================================
// Compaction History Tracker
// =============================================================================

/**
 * @brief Tracks compaction history for scheduling decisions
 */
class CompactionHistory {
public:
    struct Entry {
        uint64_t timestamp;
        CompactionStats stats;
    };

    void record(const CompactionStats& stats) {
        Entry entry;
        entry.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        entry.stats = stats;

        history_.push_back(entry);
        if (history_.size() > maxEntries_) {
            history_.pop_front();
        }
    }

    /**
     * @brief Average compaction time over last N cycles
     */
    double averageCompactionMs(size_t n = 10) const {
        if (history_.empty()) return 0;
        size_t count = std::min(n, history_.size());
        double sum = 0;
        auto it = history_.rbegin();
        for (size_t i = 0; i < count; i++, ++it) {
            sum += it->stats.totalMs;
        }
        return sum / static_cast<double>(count);
    }

    /**
     * @brief Average fragmentation reduction
     */
    double averageFragReduction(size_t n = 10) const {
        if (history_.empty()) return 0;
        size_t count = std::min(n, history_.size());
        double sum = 0;
        auto it = history_.rbegin();
        for (size_t i = 0; i < count; i++, ++it) {
            sum += it->stats.fragmentationReduction;
        }
        return sum / static_cast<double>(count);
    }

    /**
     * @brief Should we compact based on history?
     */
    bool shouldCompact(double currentFragmentation) const {
        if (history_.empty()) return currentFragmentation > 0.3;

        // Compact if fragmentation is growing
        double avgReduction = averageFragReduction();
        return currentFragmentation > 0.3 && avgReduction > 0.05;
    }

    size_t entryCount() const { return history_.size(); }

private:
    std::deque<Entry> history_;
    size_t maxEntries_ = 100;
};

} // namespace Zepra::Heap
