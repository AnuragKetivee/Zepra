// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_exclusive_access.cpp — Stop-the-world coordinator, exclusive GC access

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <functional>
#include <vector>

namespace Zepra::Heap {

class ExclusiveAccess {
public:
    ExclusiveAccess() : isExclusive_(false), waitingForExclusive_(false)
        , stoppedCount_(0), registeredCount_(0) {}

    // Register a mutator thread. Returns an ID.
    uint32_t registerMutator() {
        std::lock_guard<std::mutex> lock(mutex_);
        registeredCount_++;
        return registeredCount_ - 1;
    }

    void unregisterMutator(uint32_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (registeredCount_ > 0) registeredCount_--;
    }

    // --- GC Thread API ---

    // Acquire exclusive access: stop all mutators.
    void acquire(std::function<void()> requestStopAll,
                 std::function<bool()> allStopped) {
        std::unique_lock<std::mutex> lock(mutex_);

        assert(!isExclusive_);
        waitingForExclusive_ = true;

        // Request all mutators to stop.
        if (requestStopAll) requestStopAll();

        auto start = std::chrono::steady_clock::now();

        // Wait until all mutators have stopped.
        exclusiveCV_.wait(lock, [&]() {
            return allStopped ? allStopped() : (stoppedCount_ >= registeredCount_);
        });

        isExclusive_ = true;
        waitingForExclusive_ = false;

        auto end = std::chrono::steady_clock::now();
        stats_.totalSTWMs += std::chrono::duration<double, std::milli>(end - start).count();
        stats_.stwCount++;
    }

    // Release exclusive access: resume all mutators.
    void release(std::function<void()> resumeAll) {
        std::lock_guard<std::mutex> lock(mutex_);
        assert(isExclusive_);

        isExclusive_ = false;
        stoppedCount_ = 0;

        if (resumeAll) resumeAll();

        resumeCV_.notify_all();
    }

    // Execute a function with exclusive access.
    void withExclusive(std::function<void()> requestStopAll,
                       std::function<bool()> allStopped,
                       std::function<void()> resumeAll,
                       std::function<void()> work) {
        acquire(requestStopAll, allStopped);
        work();
        release(resumeAll);
    }

    // --- Mutator Thread API ---

    // Mutator reports it has stopped.
    void mutatorStopped() {
        std::lock_guard<std::mutex> lock(mutex_);
        stoppedCount_++;
        exclusiveCV_.notify_all();
    }

    // Mutator waits for GC to release.
    void mutatorWaitForResume() {
        std::unique_lock<std::mutex> lock(mutex_);
        resumeCV_.wait(lock, [this]() { return !isExclusive_; });
    }

    // Check if exclusive access is being requested.
    bool isExclusiveRequested() const {
        return waitingForExclusive_ || isExclusive_;
    }

    bool isExclusive() const { return isExclusive_; }

    // Timed exclusive: attempt with deadline.
    bool tryAcquire(std::function<void()> requestStopAll,
                    std::function<bool()> allStopped,
                    double timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (isExclusive_) return false;
        waitingForExclusive_ = true;

        if (requestStopAll) requestStopAll();

        bool acquired = exclusiveCV_.wait_for(lock,
            std::chrono::microseconds(static_cast<int64_t>(timeoutMs * 1000)),
            [&]() {
                return allStopped ? allStopped() : (stoppedCount_ >= registeredCount_);
            });

        if (acquired) {
            isExclusive_ = true;
            stats_.stwCount++;
        }

        waitingForExclusive_ = false;
        return acquired;
    }

    struct Stats {
        uint64_t stwCount = 0;
        double totalSTWMs = 0;

        double averageSTWMs() const {
            return stwCount > 0 ? totalSTWMs / stwCount : 0;
        }
    };

    const Stats& stats() const { return stats_; }

private:
    std::mutex mutex_;
    std::condition_variable exclusiveCV_;
    std::condition_variable resumeCV_;
    bool isExclusive_;
    bool waitingForExclusive_;
    uint32_t stoppedCount_;
    uint32_t registeredCount_;
    Stats stats_;
};

} // namespace Zepra::Heap
