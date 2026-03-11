// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file ParallelMarker.h
 * @brief Multi-threaded marking for GC
 *
 * Parallelizes the mark phase across multiple worker threads:
 * - Work-stealing deques for load balancing
 * - Atomic mark bits (CAS-based)
 * - No global lock during marking
 *
 * Throughput scales near-linearly with core count for large heaps.
 * Falls back to single-threaded marking for small heaps (<1MB).
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

namespace Zepra::Heap {

// =============================================================================
// Mark Bit Table
// =============================================================================

/**
 * @brief Bitmap for tracking mark bits
 *
 * One bit per object. Atomic CAS for thread-safe mark setting.
 * Separate from object headers to improve cache locality of marking.
 */
class MarkBitTable {
public:
    MarkBitTable() = default;

    void initialize(void* heapBase, size_t heapSize, size_t objectAlignment) {
        heapBase_ = reinterpret_cast<uintptr_t>(heapBase);
        alignment_ = objectAlignment;
        size_t numBits = heapSize / objectAlignment;
        size_t numWords = (numBits + 63) / 64;
        bits_.resize(numWords);
        clear();
    }

    /**
     * @brief Atomically set mark bit
     * @return true if the bit was newly set (first marker wins)
     */
    bool tryMark(void* object) {
        auto [wordIdx, bitIdx] = indexOf(object);
        if (wordIdx >= bits_.size()) return false;

        uint64_t mask = uint64_t(1) << bitIdx;
        uint64_t old = bits_[wordIdx].fetch_or(mask, std::memory_order_relaxed);
        return (old & mask) == 0;  // true if we set it first
    }

    bool isMarked(void* object) const {
        auto [wordIdx, bitIdx] = indexOf(object);
        if (wordIdx >= bits_.size()) return false;
        return (bits_[wordIdx].load(std::memory_order_relaxed) & (uint64_t(1) << bitIdx)) != 0;
    }

    void clear() {
        for (auto& word : bits_) {
            word.store(0, std::memory_order_relaxed);
        }
    }

    size_t markedCount() const {
        size_t count = 0;
        for (const auto& word : bits_) {
            count += __builtin_popcountll(word.load(std::memory_order_relaxed));
        }
        return count;
    }

private:
    std::pair<size_t, size_t> indexOf(void* object) const {
        uintptr_t addr = reinterpret_cast<uintptr_t>(object);
        size_t offset = (addr - heapBase_) / alignment_;
        return {offset / 64, offset % 64};
    }

    std::vector<std::atomic<uint64_t>> bits_;
    uintptr_t heapBase_ = 0;
    size_t alignment_ = 8;
};

// =============================================================================
// Work-Stealing Deque
// =============================================================================

/**
 * @brief Thread-local work queue with stealing support
 *
 * Owner pushes/pops from the bottom. Thieves steal from the top.
 * Lock-free for owner, one mutex for thieves.
 */
class WorkStealingDeque {
public:
    static constexpr size_t INITIAL_SIZE = 4096;

    WorkStealingDeque() {
        buffer_.resize(INITIAL_SIZE);
    }

    /**
     * @brief Push work item (owner only)
     */
    void push(void* item) {
        size_t b = bottom_.load(std::memory_order_relaxed);
        if (b >= buffer_.size()) {
            grow();
        }
        buffer_[b] = item;
        bottom_.store(b + 1, std::memory_order_release);
    }

    /**
     * @brief Pop work item (owner only)
     * @return nullptr if empty
     */
    void* pop() {
        size_t b = bottom_.load(std::memory_order_relaxed);
        if (b == 0) return nullptr;
        b--;
        bottom_.store(b, std::memory_order_relaxed);

        void* item = buffer_[b];

        size_t t = top_.load(std::memory_order_acquire);
        if (b > t) {
            return item;  // Still items above
        }
        if (b == t) {
            // Last item — race with stealers
            if (top_.compare_exchange_strong(t, t + 1, std::memory_order_acq_rel)) {
                bottom_.store(t + 1, std::memory_order_relaxed);
                return item;
            }
            bottom_.store(t + 1, std::memory_order_relaxed);
            return nullptr;
        }
        // b < t: was already stolen
        bottom_.store(t, std::memory_order_relaxed);
        return nullptr;
    }

    /**
     * @brief Steal work item (thieves only)
     * @return nullptr if empty
     */
    void* steal() {
        size_t t = top_.load(std::memory_order_acquire);
        size_t b = bottom_.load(std::memory_order_acquire);
        if (t >= b) return nullptr;

        void* item = buffer_[t];
        if (top_.compare_exchange_strong(t, t + 1, std::memory_order_acq_rel)) {
            return item;
        }
        return nullptr;  // Lost race
    }

    bool empty() const {
        return top_.load(std::memory_order_relaxed) >=
               bottom_.load(std::memory_order_relaxed);
    }

    size_t size() const {
        size_t t = top_.load(std::memory_order_relaxed);
        size_t b = bottom_.load(std::memory_order_relaxed);
        return b > t ? b - t : 0;
    }

private:
    void grow() {
        size_t newSize = buffer_.size() * 2;
        buffer_.resize(newSize);
    }

    std::vector<void*> buffer_;
    std::atomic<size_t> top_{0};
    std::atomic<size_t> bottom_{0};
};

// =============================================================================
// Parallel Marker Configuration
// =============================================================================

struct ParallelMarkerConfig {
    size_t numWorkers = 0;          // 0 = auto (hardware_concurrency - 1)
    size_t minHeapForParallel = 1024 * 1024;  // 1MB — below this, single-threaded
    size_t batchSize = 64;          // Objects per steal attempt
    bool enableWorkStealing = true;
};

// =============================================================================
// Parallel Marker Statistics
// =============================================================================

struct ParallelMarkerStats {
    uint64_t objectsMarked = 0;
    uint64_t bytesMarked = 0;
    uint64_t stealAttempts = 0;
    uint64_t stealSuccesses = 0;
    double durationMs = 0;
    size_t workersUsed = 0;

    double stealSuccessRate() const {
        return stealAttempts > 0
            ? static_cast<double>(stealSuccesses) / static_cast<double>(stealAttempts)
            : 0.0;
    }
};

// =============================================================================
// Object Tracer (for parallel use)
// =============================================================================

/**
 * @brief Traces references in a single object
 * Must be thread-safe (called from multiple workers).
 */
using ParallelObjectTracer = std::function<void(void* object,
    std::function<void(void* referencedObject)> pushWork)>;

// =============================================================================
// Parallel Marker
// =============================================================================

class ParallelMarker {
public:
    explicit ParallelMarker(const ParallelMarkerConfig& config = ParallelMarkerConfig{});
    ~ParallelMarker();

    // Non-copyable
    ParallelMarker(const ParallelMarker&) = delete;
    ParallelMarker& operator=(const ParallelMarker&) = delete;

    /**
     * @brief Initialize mark bit table
     */
    void initialize(void* heapBase, size_t heapSize, size_t objectAlignment = 8);

    /**
     * @brief Run parallel marking
     *
     * @param roots Initial root set
     * @param tracer Called for each marked object to discover references
     */
    void mark(const std::vector<void*>& roots, ParallelObjectTracer tracer);

    /**
     * @brief Check if object is marked
     */
    bool isMarked(void* object) const { return markBits_.isMarked(object); }

    /**
     * @brief Clear all marks (before new GC cycle)
     */
    void clearMarks() { markBits_.clear(); }

    /**
     * @brief Statistics from last mark phase
     */
    const ParallelMarkerStats& stats() const { return stats_; }

    /**
     * @brief Total objects marked
     */
    size_t markedCount() const { return markBits_.markedCount(); }

private:
    void workerLoop(size_t workerId, ParallelObjectTracer& tracer);
    void singleThreadedMark(const std::vector<void*>& roots,
                             ParallelObjectTracer& tracer);

    ParallelMarkerConfig config_;
    ParallelMarkerStats stats_;
    mutable MarkBitTable markBits_;

    // Worker threads
    std::vector<std::thread> workers_;
    std::vector<WorkStealingDeque> workerQueues_;

    // Synchronization
    std::atomic<bool> markingDone_{false};
    std::atomic<size_t> activeWorkers_{0};
    std::mutex startMutex_;
    std::condition_variable startCV_;
    std::atomic<bool> started_{false};

    // Shared state
    ParallelObjectTracer* currentTracer_ = nullptr;
};

// =============================================================================
// Implementation
// =============================================================================

inline ParallelMarker::ParallelMarker(const ParallelMarkerConfig& config)
    : config_(config) {
    if (config_.numWorkers == 0) {
        unsigned hw = std::thread::hardware_concurrency();
        config_.numWorkers = hw > 1 ? hw - 1 : 1;
    }
    workerQueues_.resize(config_.numWorkers);
}

inline ParallelMarker::~ParallelMarker() {
    markingDone_ = true;
    startCV_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

inline void ParallelMarker::initialize(void* heapBase, size_t heapSize,
                                        size_t objectAlignment) {
    markBits_.initialize(heapBase, heapSize, objectAlignment);
}

inline void ParallelMarker::mark(const std::vector<void*>& roots,
                                  ParallelObjectTracer tracer) {
    stats_ = {};
    auto startTime = std::chrono::steady_clock::now();

    // For small heaps, single-threaded is faster (no thread overhead)
    if (roots.size() < 100) {
        singleThreadedMark(roots, tracer);
    } else {
        // Distribute roots across worker queues
        for (size_t i = 0; i < roots.size(); i++) {
            size_t workerIdx = i % config_.numWorkers;
            if (markBits_.tryMark(roots[i])) {
                workerQueues_[workerIdx].push(roots[i]);
            }
        }

        stats_.workersUsed = config_.numWorkers;
        currentTracer_ = &tracer;
        markingDone_ = false;
        started_ = true;
        activeWorkers_ = config_.numWorkers;

        // Launch workers
        workers_.clear();
        for (size_t i = 0; i < config_.numWorkers; i++) {
            workers_.emplace_back([this, i, &tracer]() {
                workerLoop(i, tracer);
            });
        }

        // Wait for completion
        for (auto& w : workers_) {
            w.join();
        }
        workers_.clear();
    }

    auto endTime = std::chrono::steady_clock::now();
    stats_.durationMs = std::chrono::duration<double, std::milli>(
        endTime - startTime).count();
}

inline void ParallelMarker::workerLoop(size_t workerId,
                                        ParallelObjectTracer& tracer) {
    auto& myQueue = workerQueues_[workerId];

    while (true) {
        // Try own queue first
        void* obj = myQueue.pop();

        if (!obj && config_.enableWorkStealing) {
            // Try stealing from other workers
            for (size_t i = 0; i < config_.numWorkers; i++) {
                if (i == workerId) continue;
                stats_.stealAttempts++;
                obj = workerQueues_[i].steal();
                if (obj) {
                    stats_.stealSuccesses++;
                    break;
                }
            }
        }

        if (!obj) {
            // Check if all workers are idle
            size_t remaining = activeWorkers_.fetch_sub(1) - 1;
            if (remaining == 0) {
                // Last active worker — check if truly done
                bool allEmpty = true;
                for (size_t i = 0; i < config_.numWorkers; i++) {
                    if (!workerQueues_[i].empty()) {
                        allEmpty = false;
                        break;
                    }
                }
                if (allEmpty) {
                    markingDone_ = true;
                    return;
                }
            }

            // Briefly yield, then retry
            std::this_thread::yield();
            activeWorkers_++;

            if (markingDone_) return;
            continue;
        }

        // Process object
        stats_.objectsMarked++;
        tracer(obj, [this, workerId](void* ref) {
            if (ref && markBits_.tryMark(ref)) {
                workerQueues_[workerId].push(ref);
            }
        });
    }
}

inline void ParallelMarker::singleThreadedMark(
    const std::vector<void*>& roots,
    ParallelObjectTracer& tracer
) {
    stats_.workersUsed = 1;
    std::deque<void*> worklist;

    for (void* root : roots) {
        if (root && markBits_.tryMark(root)) {
            worklist.push_back(root);
        }
    }

    while (!worklist.empty()) {
        void* obj = worklist.front();
        worklist.pop_front();

        stats_.objectsMarked++;
        tracer(obj, [this, &worklist](void* ref) {
            if (ref && markBits_.tryMark(ref)) {
                worklist.push_back(ref);
            }
        });
    }
}

} // namespace Zepra::Heap
