// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_memory_counters.cpp — Real-time GC memory counters

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <atomic>
#include <chrono>
#include <mutex>

namespace Zepra::Heap {

class GCMemoryCounters {
public:
    GCMemoryCounters() { reset(); }

    // Heap size: total committed memory.
    void setHeapSize(size_t bytes) { heapSize_.store(bytes, std::memory_order_relaxed); }
    size_t heapSize() const { return heapSize_.load(std::memory_order_relaxed); }

    // Live bytes: bytes occupied by reachable objects.
    void setLiveBytes(size_t bytes) { liveBytes_.store(bytes, std::memory_order_relaxed); }
    size_t liveBytes() const { return liveBytes_.load(std::memory_order_relaxed); }

    // RSS: resident set size (physical memory used by process).
    void setRSS(size_t bytes) { rss_.store(bytes, std::memory_order_relaxed); }
    size_t rss() const { return rss_.load(std::memory_order_relaxed); }

    // Peak values.
    void updatePeakHeap() {
        size_t current = heapSize();
        size_t peak = peakHeapSize_.load(std::memory_order_relaxed);
        while (current > peak) {
            if (peakHeapSize_.compare_exchange_weak(peak, current)) break;
        }
    }

    void updatePeakRSS() {
        size_t current = rss();
        size_t peak = peakRSS_.load(std::memory_order_relaxed);
        while (current > peak) {
            if (peakRSS_.compare_exchange_weak(peak, current)) break;
        }
    }

    size_t peakHeapSize() const { return peakHeapSize_.load(std::memory_order_relaxed); }
    size_t peakRSS() const { return peakRSS_.load(std::memory_order_relaxed); }

    // Allocation counters (since last GC and total).
    void recordAllocation(size_t bytes) {
        allocatedSinceGC_.fetch_add(bytes, std::memory_order_relaxed);
        totalAllocated_.fetch_add(bytes, std::memory_order_relaxed);
        allocationCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void resetAllocationCounter() {
        allocatedSinceGC_.store(0, std::memory_order_relaxed);
    }

    size_t allocatedSinceGC() const { return allocatedSinceGC_.load(std::memory_order_relaxed); }
    size_t totalAllocated() const { return totalAllocated_.load(std::memory_order_relaxed); }
    uint64_t allocationCount() const { return allocationCount_.load(std::memory_order_relaxed); }

    // Free bytes = heap - live.
    size_t freeBytes() const {
        size_t heap = heapSize();
        size_t live = liveBytes();
        return heap > live ? heap - live : 0;
    }

    // Utilization = live / heap.
    double utilization() const {
        size_t heap = heapSize();
        return heap > 0 ? static_cast<double>(liveBytes()) / heap : 0;
    }

    // Fragmentation = (heap - live) / heap.
    double fragmentation() const {
        return 1.0 - utilization();
    }

    // Nursery counters.
    void setNurseryUsed(size_t bytes) { nurseryUsed_.store(bytes, std::memory_order_relaxed); }
    void setNurseryCapacity(size_t bytes) { nurseryCapacity_.store(bytes, std::memory_order_relaxed); }
    size_t nurseryUsed() const { return nurseryUsed_.load(std::memory_order_relaxed); }
    size_t nurseryCapacity() const { return nurseryCapacity_.load(std::memory_order_relaxed); }

    double nurseryUtilization() const {
        size_t cap = nurseryCapacity();
        return cap > 0 ? static_cast<double>(nurseryUsed()) / cap : 0;
    }

    // Large object space.
    void setLargeObjectBytes(size_t bytes) { largeObjectBytes_.store(bytes, std::memory_order_relaxed); }
    size_t largeObjectBytes() const { return largeObjectBytes_.load(std::memory_order_relaxed); }

    // Zone count.
    void setZoneCount(uint32_t count) { zoneCount_.store(count, std::memory_order_relaxed); }
    uint32_t zoneCount() const { return zoneCount_.load(std::memory_order_relaxed); }

    // Arena count.
    void setArenaCount(uint32_t count) { arenaCount_.store(count, std::memory_order_relaxed); }
    uint32_t arenaCount() const { return arenaCount_.load(std::memory_order_relaxed); }

    void reset() {
        heapSize_.store(0, std::memory_order_relaxed);
        liveBytes_.store(0, std::memory_order_relaxed);
        rss_.store(0, std::memory_order_relaxed);
        peakHeapSize_.store(0, std::memory_order_relaxed);
        peakRSS_.store(0, std::memory_order_relaxed);
        allocatedSinceGC_.store(0, std::memory_order_relaxed);
        totalAllocated_.store(0, std::memory_order_relaxed);
        allocationCount_.store(0, std::memory_order_relaxed);
        nurseryUsed_.store(0, std::memory_order_relaxed);
        nurseryCapacity_.store(0, std::memory_order_relaxed);
        largeObjectBytes_.store(0, std::memory_order_relaxed);
        zoneCount_.store(0, std::memory_order_relaxed);
        arenaCount_.store(0, std::memory_order_relaxed);
    }

    struct Snapshot {
        size_t heapSize;
        size_t liveBytes;
        size_t rss;
        size_t freeBytes;
        size_t nurseryUsed;
        size_t allocatedSinceGC;
        double utilization;
        double fragmentation;
        uint32_t zoneCount;
        uint32_t arenaCount;
    };

    Snapshot snapshot() const {
        return {heapSize(), liveBytes(), rss(), freeBytes(), nurseryUsed(),
                allocatedSinceGC(), utilization(), fragmentation(),
                zoneCount(), arenaCount()};
    }

private:
    std::atomic<size_t> heapSize_;
    std::atomic<size_t> liveBytes_;
    std::atomic<size_t> rss_;
    std::atomic<size_t> peakHeapSize_;
    std::atomic<size_t> peakRSS_;
    std::atomic<size_t> allocatedSinceGC_;
    std::atomic<size_t> totalAllocated_;
    std::atomic<uint64_t> allocationCount_;
    std::atomic<size_t> nurseryUsed_;
    std::atomic<size_t> nurseryCapacity_;
    std::atomic<size_t> largeObjectBytes_;
    std::atomic<uint32_t> zoneCount_;
    std::atomic<uint32_t> arenaCount_;
};

} // namespace Zepra::Heap
