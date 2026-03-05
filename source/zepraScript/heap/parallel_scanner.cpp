/**
 * @file parallel_scanner.cpp
 * @brief Parallel root scanning and stack walking
 *
 * Splits root scanning across multiple threads:
 * - Each mutator thread's stack is scanned by that thread (via handshake)
 *   or by a GC worker thread (if the mutator is suspended)
 * - Global roots (global object, builtin prototypes) are split by range
 * - Handle scopes are scanned per-thread
 *
 * Stack scanning strategies:
 * 1. Precise: use GC maps from JIT to identify exact reference slots
 * 2. Conservative: scan the entire stack for pointer-like values
 * 3. Hybrid: precise for JIT frames, conservative for C++ frames
 *
 * This file implements all three strategies and the parallel dispatch.
 */

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <functional>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <memory>
#include <algorithm>

namespace Zepra::Heap {

// =============================================================================
// Stack Frame Descriptor
// =============================================================================

/**
 * @brief Describes a single call frame for precise stack scanning
 *
 * JIT code emits these descriptors so the GC knows which stack
 * slots contain live references at each safe-point (return address).
 */
struct StackFrameDescriptor {
    uintptr_t returnAddress;        // PC at this safe-point
    uint16_t frameSize;             // Total frame size in bytes
    uint16_t refSlotCount;          // Number of reference-containing slots
    int16_t refSlotOffsets[32];     // Offsets from frame base (max 32 refs/frame)
    uint32_t functionId;            // For debugging: which function

    bool containsSlotAt(int16_t offset) const {
        for (uint16_t i = 0; i < refSlotCount; i++) {
            if (refSlotOffsets[i] == offset) return true;
        }
        return false;
    }
};

// =============================================================================
// GC Map Table
// =============================================================================

/**
 * @brief Global table of stack frame descriptors (populated by JIT)
 *
 * Sorted by return address for binary search during stack walking.
 */
class GCMapTable {
public:
    void registerDescriptor(const StackFrameDescriptor& desc) {
        std::lock_guard<std::mutex> lock(mutex_);
        descriptors_.push_back(desc);
        sorted_ = false;
    }

    void removeDescriptorsForFunction(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        descriptors_.erase(
            std::remove_if(descriptors_.begin(), descriptors_.end(),
                [functionId](const StackFrameDescriptor& d) {
                    return d.functionId == functionId;
                }),
            descriptors_.end());
    }

    const StackFrameDescriptor* findByReturnAddress(uintptr_t pc) const {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureSorted();

        // Binary search
        size_t lo = 0, hi = descriptors_.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (descriptors_[mid].returnAddress < pc) {
                lo = mid + 1;
            } else if (descriptors_[mid].returnAddress > pc) {
                hi = mid;
            } else {
                return &descriptors_[mid];
            }
        }
        return nullptr;
    }

    size_t descriptorCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return descriptors_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        descriptors_.clear();
    }

private:
    void ensureSorted() const {
        if (sorted_) return;
        std::sort(descriptors_.begin(), descriptors_.end(),
            [](const StackFrameDescriptor& a, const StackFrameDescriptor& b) {
                return a.returnAddress < b.returnAddress;
            });
        sorted_ = true;
    }

    mutable std::vector<StackFrameDescriptor> descriptors_;
    mutable bool sorted_ = false;
    mutable std::mutex mutex_;
};

// =============================================================================
// Stack Walker
// =============================================================================

/**
 * @brief Walks a thread's stack and visits reference slots
 *
 * Supports three modes:
 * 1. Precise: uses GCMapTable to find exact reference slots
 * 2. Conservative: treats all aligned pointer-like values as roots
 * 3. Hybrid: precise for frames with GC maps, conservative for others
 */
class StackWalker {
public:
    enum class Mode { Precise, Conservative, Hybrid };

    struct Config {
        Mode mode;
        uintptr_t heapLow;     // Lowest valid heap address
        uintptr_t heapHigh;    // Highest valid heap address

        Config()
            : mode(Mode::Hybrid)
            , heapLow(0)
            , heapHigh(UINTPTR_MAX) {}
    };

    struct ScanStats {
        size_t framesScanned;
        size_t preciseFrames;
        size_t conservativeFrames;
        size_t slotsVisited;
        size_t candidatePointers;
        double durationMs;
    };

    explicit StackWalker(const Config& config, GCMapTable* gcMapTable = nullptr)
        : config_(config), gcMapTable_(gcMapTable) {}

    /**
     * @brief Scan a stack region
     * @param stackTop Current stack pointer (low address)
     * @param stackBase Base of stack (high address, grows down on x86)
     * @param visitor Callback for each reference slot found
     */
    ScanStats scan(void* stackTop, void* stackBase,
                   std::function<void(void** slot)> visitor) {
        ScanStats stats{};
        auto start = std::chrono::steady_clock::now();

        if (!stackTop || !stackBase) return stats;

        auto topAddr = reinterpret_cast<uintptr_t>(stackTop);
        auto baseAddr = reinterpret_cast<uintptr_t>(stackBase);

        // Ensure correct ordering (stack grows down on x86/arm)
        if (topAddr > baseAddr) std::swap(topAddr, baseAddr);

        switch (config_.mode) {
            case Mode::Precise:
                scanPrecise(topAddr, baseAddr, visitor, stats);
                break;
            case Mode::Conservative:
                scanConservative(topAddr, baseAddr, visitor, stats);
                break;
            case Mode::Hybrid:
                scanHybrid(topAddr, baseAddr, visitor, stats);
                break;
        }

        stats.durationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        return stats;
    }

private:
    /**
     * @brief Precise scanning using GC maps
     */
    void scanPrecise(uintptr_t top, uintptr_t base,
                     std::function<void(void** slot)>& visitor,
                     ScanStats& stats) {
        if (!gcMapTable_) return;

        // Walk frame chain using frame pointers
        // Assumes x86_64 frame layout: [rbp][return addr][locals...]
        uintptr_t fp = top;  // Start from current frame pointer

        while (fp >= top && fp < base) {
            stats.framesScanned++;

            // Return address is at fp + sizeof(void*)
            uintptr_t retAddr = *reinterpret_cast<uintptr_t*>(fp + sizeof(void*));

            const StackFrameDescriptor* desc =
                gcMapTable_->findByReturnAddress(retAddr);

            if (desc) {
                stats.preciseFrames++;
                // Visit each reference slot described by the GC map
                for (uint16_t i = 0; i < desc->refSlotCount; i++) {
                    auto* slot = reinterpret_cast<void**>(
                        fp + desc->refSlotOffsets[i]);
                    if (*slot) {
                        visitor(slot);
                        stats.slotsVisited++;
                    }
                }
            }

            // Walk to next frame (saved rbp)
            uintptr_t nextFp = *reinterpret_cast<uintptr_t*>(fp);
            if (nextFp <= fp) break;  // Prevent infinite loops
            fp = nextFp;
        }
    }

    /**
     * @brief Conservative scanning: every aligned word that looks like
     * a heap pointer is treated as a root
     */
    void scanConservative(uintptr_t top, uintptr_t base,
                          std::function<void(void** slot)>& visitor,
                          ScanStats& stats) {
        // Align to pointer size
        top = (top + sizeof(void*) - 1) & ~(sizeof(void*) - 1);

        for (uintptr_t addr = top; addr < base; addr += sizeof(void*)) {
            auto* slot = reinterpret_cast<void**>(addr);
            uintptr_t value = reinterpret_cast<uintptr_t>(*slot);

            stats.slotsVisited++;

            // Check if value looks like a heap pointer
            if (value >= config_.heapLow && value < config_.heapHigh) {
                // Additional heuristics:
                // - Must be aligned to at least 8 bytes
                // - Must not be 0 or small integer
                if ((value & 7) == 0 && value > 0x1000) {
                    visitor(slot);
                    stats.candidatePointers++;
                }
            }
        }

        stats.conservativeFrames++;
    }

    /**
     * @brief Hybrid: use GC maps where available, conservative otherwise
     */
    void scanHybrid(uintptr_t top, uintptr_t base,
                    std::function<void(void** slot)>& visitor,
                    ScanStats& stats) {
        if (!gcMapTable_) {
            // Fall back to conservative
            scanConservative(top, base, visitor, stats);
            return;
        }

        // Walk frames with frame pointer chain
        uintptr_t fp = top;
        uintptr_t lastPreciseFp = top;

        while (fp >= top && fp < base) {
            stats.framesScanned++;

            uintptr_t retAddr = *reinterpret_cast<uintptr_t*>(fp + sizeof(void*));
            const StackFrameDescriptor* desc =
                gcMapTable_->findByReturnAddress(retAddr);

            if (desc) {
                // Precise: scan only declared reference slots
                stats.preciseFrames++;

                // Conservative-scan the gap between last precise frame and this one
                if (fp > lastPreciseFp + sizeof(void*) * 2) {
                    scanConservative(lastPreciseFp, fp, visitor, stats);
                }

                for (uint16_t i = 0; i < desc->refSlotCount; i++) {
                    auto* slot = reinterpret_cast<void**>(
                        fp + desc->refSlotOffsets[i]);
                    if (*slot) {
                        visitor(slot);
                        stats.slotsVisited++;
                    }
                }

                lastPreciseFp = fp + desc->frameSize;
            } else {
                stats.conservativeFrames++;
            }

            uintptr_t nextFp = *reinterpret_cast<uintptr_t*>(fp);
            if (nextFp <= fp) break;
            fp = nextFp;
        }

        // Conservative-scan remaining stack after last precise frame
        if (lastPreciseFp < base) {
            scanConservative(lastPreciseFp, base, visitor, stats);
        }
    }

    Config config_;
    GCMapTable* gcMapTable_;
};

// =============================================================================
// Parallel Root Scanner
// =============================================================================

/**
 * @brief Dispatches root scanning across multiple worker threads
 */
class ParallelScanner {
public:
    struct Config {
        size_t workerCount;
        StackWalker::Mode stackScanMode;

        Config()
            : workerCount(2)
            , stackScanMode(StackWalker::Mode::Hybrid) {}
    };

    struct ScanResult {
        size_t threadsScanned;
        size_t totalSlots;
        size_t totalCandidates;
        double totalMs;
        std::vector<StackWalker::ScanStats> perThreadStats;
    };

    // Thread info needed for scanning
    struct ThreadScanInfo {
        uint64_t threadId;
        void* stackTop;
        void* stackBase;
        void* handleScopeHead;
    };

    using SlotVisitor = std::function<void(void** slot)>;

    // Callbacks
    struct Callbacks {
        // Get list of threads to scan
        std::function<std::vector<ThreadScanInfo>()> getThreads;

        // Scan a thread's handle scopes
        std::function<void(void* handleScopeHead, SlotVisitor)> scanHandleScopes;

        // Scan global roots (global object, builtins)
        std::function<void(SlotVisitor)> scanGlobalRoots;
    };

    explicit ParallelScanner(const Config& config = Config{});

    void setCallbacks(Callbacks callbacks) { cb_ = std::move(callbacks); }
    void setGCMapTable(GCMapTable* table) { gcMapTable_ = table; }

    /**
     * @brief Set heap address range for conservative scanning
     */
    void setHeapRange(uintptr_t low, uintptr_t high) {
        heapLow_ = low;
        heapHigh_ = high;
    }

    /**
     * @brief Scan all roots in parallel
     */
    ScanResult scanAll(SlotVisitor visitor);

    /**
     * @brief Scan a single thread's roots
     */
    StackWalker::ScanStats scanThread(const ThreadScanInfo& info,
                                       SlotVisitor visitor);

private:
    Config config_;
    Callbacks cb_;
    GCMapTable* gcMapTable_ = nullptr;
    uintptr_t heapLow_ = 0;
    uintptr_t heapHigh_ = UINTPTR_MAX;
};

// =============================================================================
// Implementation
// =============================================================================

inline ParallelScanner::ParallelScanner(const Config& config)
    : config_(config) {}

inline ParallelScanner::ScanResult ParallelScanner::scanAll(
    SlotVisitor visitor
) {
    ScanResult result{};
    auto start = std::chrono::steady_clock::now();

    // Get threads to scan
    std::vector<ThreadScanInfo> threads;
    if (cb_.getThreads) {
        threads = cb_.getThreads();
    }

    result.threadsScanned = threads.size();

    // The visitor must be thread-safe if we're scanning in parallel
    std::mutex visitorMutex;
    auto safeVisitor = [&](void** slot) {
        std::lock_guard<std::mutex> lock(visitorMutex);
        visitor(slot);
    };

    if (config_.workerCount <= 1 || threads.size() <= 1) {
        // Single-threaded scan
        for (const auto& info : threads) {
            auto stats = scanThread(info, safeVisitor);
            result.perThreadStats.push_back(stats);
            result.totalSlots += stats.slotsVisited;
            result.totalCandidates += stats.candidatePointers;
        }
    } else {
        // Parallel scan: each worker handles a subset of threads
        std::vector<std::thread> workers;
        std::mutex resultMutex;
        std::atomic<size_t> nextThread{0};

        for (size_t w = 0; w < config_.workerCount; w++) {
            workers.emplace_back([&]() {
                while (true) {
                    size_t idx = nextThread.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= threads.size()) break;

                    auto stats = scanThread(threads[idx], safeVisitor);

                    std::lock_guard<std::mutex> lock(resultMutex);
                    result.perThreadStats.push_back(stats);
                    result.totalSlots += stats.slotsVisited;
                    result.totalCandidates += stats.candidatePointers;
                }
            });
        }

        for (auto& w : workers) {
            w.join();
        }
    }

    // Scan global roots (always single-threaded)
    if (cb_.scanGlobalRoots) {
        cb_.scanGlobalRoots(visitor);
    }

    result.totalMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();

    return result;
}

inline StackWalker::ScanStats ParallelScanner::scanThread(
    const ThreadScanInfo& info, SlotVisitor visitor
) {
    StackWalker::Config walkerConfig;
    walkerConfig.mode = config_.stackScanMode;
    walkerConfig.heapLow = heapLow_;
    walkerConfig.heapHigh = heapHigh_;

    StackWalker walker(walkerConfig, gcMapTable_);
    auto stats = walker.scan(info.stackTop, info.stackBase, visitor);

    // Also scan handle scopes
    if (cb_.scanHandleScopes && info.handleScopeHead) {
        cb_.scanHandleScopes(info.handleScopeHead, visitor);
    }

    return stats;
}

} // namespace Zepra::Heap
