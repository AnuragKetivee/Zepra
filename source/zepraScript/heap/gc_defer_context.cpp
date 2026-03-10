// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_defer_context.cpp — RAII GC deferral, deferred allocation retry queue

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>

namespace Zepra::Heap {

// Thread-local GC deferral depth. GC is blocked while depth > 0.
class DeferGC {
public:
    static void enter() { depth_++; }
    static void leave() { if (depth_ > 0) depth_--; }
    static bool isDeferred() { return depth_ > 0; }
    static uint32_t depth() { return depth_; }

private:
    static thread_local uint32_t depth_;
};

thread_local uint32_t DeferGC::depth_ = 0;

// RAII guard: prevents GC for the duration of the scope.
class NoGCScope {
public:
    NoGCScope() { DeferGC::enter(); }
    ~NoGCScope() { DeferGC::leave(); }

    NoGCScope(const NoGCScope&) = delete;
    NoGCScope& operator=(const NoGCScope&) = delete;
};

// Deferred allocation: queue allocations that failed during NoGC scope.
struct DeferredAllocation {
    size_t size;
    uint16_t sizeClass;
    uint32_t zoneId;
    void** resultSlot;   // Where to write the allocated pointer

    DeferredAllocation() : size(0), sizeClass(0), zoneId(0), resultSlot(nullptr) {}
};

class DeferredAllocationQueue {
public:
    void enqueue(size_t size, uint16_t sizeClass, uint32_t zoneId, void** resultSlot) {
        std::lock_guard<std::mutex> lock(mutex_);
        DeferredAllocation alloc;
        alloc.size = size;
        alloc.sizeClass = sizeClass;
        alloc.zoneId = zoneId;
        alloc.resultSlot = resultSlot;
        queue_.push_back(alloc);
    }

    // Retry all deferred allocations after GC completes.
    size_t retryAll(std::function<void*(size_t size, uint16_t sizeClass, uint32_t zoneId)> allocator) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t retried = 0;

        for (auto& alloc : queue_) {
            void* ptr = allocator(alloc.size, alloc.sizeClass, alloc.zoneId);
            if (alloc.resultSlot) *alloc.resultSlot = ptr;
            retried++;
        }

        queue_.clear();
        return retried;
    }

    size_t pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool hasPending() const { return pending() > 0; }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::vector<DeferredAllocation> queue_;
};

// GC deferral context: coordinates deferral state and deferred allocations.
class GCDeferContext {
public:
    bool isGCDeferred() const {
        return DeferGC::isDeferred() || globalDefer_.load(std::memory_order_relaxed);
    }

    // Global deferral (e.g., during critical section across all threads).
    void globalDefer() { globalDefer_.store(true, std::memory_order_release); }
    void globalUndefer() { globalDefer_.store(false, std::memory_order_release); }

    DeferredAllocationQueue& allocationQueue() { return allocQueue_; }

    // Record a GC request that arrived during deferral.
    void recordPendingGC() { pendingGC_ = true; }
    bool hasPendingGC() const { return pendingGC_; }
    void clearPendingGC() { pendingGC_ = false; }

    struct Stats {
        uint64_t deferrals = 0;
        uint64_t deferredAllocations = 0;
        uint64_t pendingGCs = 0;
    };

    void recordDeferral() { stats_.deferrals++; }
    void recordDeferredAllocation() { stats_.deferredAllocations++; }
    const Stats& stats() const { return stats_; }

private:
    std::atomic<bool> globalDefer_{false};
    DeferredAllocationQueue allocQueue_;
    bool pendingGC_ = false;
    Stats stats_;
};

} // namespace Zepra::Heap
