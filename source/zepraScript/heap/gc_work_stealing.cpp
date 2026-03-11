// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_work_stealing.cpp — Lock-free work-stealing deque for worklist distribution

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <atomic>
#include <cstring>
#include <vector>

namespace Zepra::Heap {

// Chase-Lev work-stealing deque.
// Owner pushes/pops from bottom. Thieves steal from top.
template<typename T, size_t Capacity = 8192>
class WorkStealingDeque {
public:
    WorkStealingDeque() : top_(0), bottom_(0) {
        memset(buffer_, 0, sizeof(buffer_));
    }

    // Owner: push to bottom.
    bool push(const T& item) {
        int64_t b = bottom_.load(std::memory_order_relaxed);
        int64_t t = top_.load(std::memory_order_acquire);

        if (b - t >= static_cast<int64_t>(Capacity)) {
            return false;  // Full.
        }

        buffer_[b % Capacity] = item;
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
        return true;
    }

    // Owner: pop from bottom.
    bool pop(T& item) {
        int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t t = top_.load(std::memory_order_relaxed);

        if (t <= b) {
            item = buffer_[b % Capacity];
            if (t == b) {
                // Last element — compete with thieves.
                if (!top_.compare_exchange_strong(t, t + 1,
                        std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    // Thief got it.
                    bottom_.store(t + 1, std::memory_order_relaxed);
                    return false;
                }
                bottom_.store(t + 1, std::memory_order_relaxed);
            }
            return true;
        } else {
            // Empty.
            bottom_.store(t, std::memory_order_relaxed);
            return false;
        }
    }

    // Thief: steal from top.
    bool steal(T& item) {
        int64_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t b = bottom_.load(std::memory_order_acquire);

        if (t >= b) return false;  // Empty.

        item = buffer_[t % Capacity];

        if (!top_.compare_exchange_strong(t, t + 1,
                std::memory_order_seq_cst, std::memory_order_relaxed)) {
            return false;  // Lost race.
        }

        return true;
    }

    int64_t size() const {
        int64_t b = bottom_.load(std::memory_order_relaxed);
        int64_t t = top_.load(std::memory_order_relaxed);
        return b > t ? b - t : 0;
    }

    bool isEmpty() const { return size() == 0; }

    void clear() {
        top_.store(0, std::memory_order_relaxed);
        bottom_.store(0, std::memory_order_relaxed);
    }

private:
    T buffer_[Capacity];
    std::atomic<int64_t> top_;
    std::atomic<int64_t> bottom_;
};

// Multi-deque work distributor: N deques, N workers, global steal policy.
template<typename T, size_t Capacity = 8192>
class WorkDistributor {
public:
    explicit WorkDistributor(uint32_t workerCount) : workerCount_(workerCount) {
        deques_.resize(workerCount);
    }

    bool push(uint32_t workerId, const T& item) {
        assert(workerId < workerCount_);
        return deques_[workerId].push(item);
    }

    bool pop(uint32_t workerId, T& item) {
        assert(workerId < workerCount_);

        // Try own deque first.
        if (deques_[workerId].pop(item)) return true;

        // Steal from others.
        return stealFromOther(workerId, item);
    }

    bool stealFromOther(uint32_t workerId, T& item) {
        for (uint32_t offset = 1; offset < workerCount_; offset++) {
            uint32_t victim = (workerId + offset) % workerCount_;
            if (deques_[victim].steal(item)) {
                stealCount_++;
                return true;
            }
        }
        return false;
    }

    int64_t totalSize() const {
        int64_t total = 0;
        for (auto& d : deques_) total += d.size();
        return total;
    }

    bool allEmpty() const {
        for (auto& d : deques_) {
            if (!d.isEmpty()) return false;
        }
        return true;
    }

    void clear() {
        for (auto& d : deques_) d.clear();
    }

    uint32_t workerCount() const { return workerCount_; }
    uint64_t stealCount() const { return stealCount_; }

private:
    uint32_t workerCount_;
    std::vector<WorkStealingDeque<T, Capacity>> deques_;
    uint64_t stealCount_ = 0;
};

} // namespace Zepra::Heap
