// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file SafePoint.h
 * @brief Safe-point mechanism for GC thread coordination
 *
 * GC requires all mutator threads to be at safe-points before collection.
 * A safe-point is a position in execution where:
 * 1. No raw pointers to heap objects exist (only handles)
 * 2. All reference state is consistent (no partial writes)
 * 3. Stack frame info is available for stack scanning
 *
 * Implementation:
 * - Polling: mutator checks a flag at loop back-edges and function entry
 * - Trap: modify a guard page to trigger a SIGSEGV at the next poll
 * - Signal: send SIGUSR1 to force threads to check
 *
 * When the GC requests a safe-point:
 * 1. Set the global safe-point flag
 * 2. Wait for all threads to reach safe-points
 * 3. Perform GC work
 * 4. Release threads
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>
#include <vector>
#include <unordered_map>
#include <chrono>

namespace Zepra::Heap {

// =============================================================================
// Thread State
// =============================================================================

enum class ThreadState : uint8_t {
    Running,        // Executing mutator code
    AtSafePoint,    // Stopped at safe-point, waiting for GC
    InNative,       // In native (C++) code — treated as safe
    Blocked,        // Blocked on I/O, lock, etc. — treated as safe
    Terminated      // Thread has exited
};

// =============================================================================
// Thread Descriptor
// =============================================================================

struct ThreadDescriptor {
    std::thread::id id;
    std::string name;
    ThreadState state = ThreadState::Running;

    // Stack info for scanning
    void* stackBase = nullptr;
    void* stackTop = nullptr;  // Set at safe-point entry

    // Safe-point state
    std::atomic<bool> safePointRequested{false};
    std::atomic<bool> atSafePoint{false};

    // Statistics
    uint64_t safePointsReached = 0;
    double totalSafePointTimeMs = 0;

    bool isSafe() const {
        return state == ThreadState::AtSafePoint ||
               state == ThreadState::InNative ||
               state == ThreadState::Blocked ||
               state == ThreadState::Terminated;
    }
};

// =============================================================================
// Safe-Point Statistics
// =============================================================================

struct SafePointStats {
    uint64_t totalRequests = 0;
    uint64_t totalTimeToSafePointUs = 0;
    uint64_t maxTimeToSafePointUs = 0;
    double avgTimeToSafePointUs = 0;
    uint64_t threadsSuspended = 0;
    uint64_t timeoutEvents = 0;
};

// =============================================================================
// Safe-Point Manager
// =============================================================================

class SafePointManager {
public:
    SafePointManager();
    ~SafePointManager();

    SafePointManager(const SafePointManager&) = delete;
    SafePointManager& operator=(const SafePointManager&) = delete;

    // -------------------------------------------------------------------------
    // Thread Registration
    // -------------------------------------------------------------------------

    /**
     * @brief Register the current thread with the safe-point system
     */
    void registerThread(const std::string& name = "",
                         void* stackBase = nullptr);

    /**
     * @brief Unregister the current thread
     */
    void unregisterThread();

    /**
     * @brief Get descriptor for current thread
     */
    ThreadDescriptor* currentThread();

    // -------------------------------------------------------------------------
    // Safe-Point Polling (called by mutator)
    // -------------------------------------------------------------------------

    /**
     * @brief Check if a safe-point has been requested
     * Called at loop back-edges and function entry.
     * Fast path: single atomic load (zero overhead if no GC requested).
     */
    bool needsSafePoint() const {
        return globalSafePointRequested_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Enter safe-point (block until GC completes)
     * Called when needsSafePoint() returns true.
     */
    void enterSafePoint();

    /**
     * @brief Enter native code (treated as always-safe)
     * Stack won't be scanned in this state — must not hold raw pointers.
     */
    void enterNative();

    /**
     * @brief Leave native code
     * May block if GC is in progress.
     */
    void leaveNative();

    /**
     * @brief Mark current thread as blocked
     */
    void enterBlocked();
    void leaveBlocked();

    // -------------------------------------------------------------------------
    // GC-Side (called by GC thread)
    // -------------------------------------------------------------------------

    /**
     * @brief Request all threads to reach safe-points
     * Blocks until all threads are safe.
     * @param timeoutMs Maximum time to wait (-1 = infinite)
     * @return true if all threads reached safe-points within timeout
     */
    bool requestSafePoint(int64_t timeoutMs = 5000);

    /**
     * @brief Release all threads from safe-points
     */
    void releaseSafePoint();

    /**
     * @brief Execute a callback with all threads at safe-points
     * Convenience wrapper for requestSafePoint + callback + releaseSafePoint.
     */
    bool withAllThreadsStopped(std::function<void()> callback,
                                int64_t timeoutMs = 5000);

    /**
     * @brief Iterate all threads for root scanning
     */
    void forEachThread(std::function<void(ThreadDescriptor& thread)> callback);

    /**
     * @brief Number of registered threads
     */
    size_t threadCount() const;

    /**
     * @brief Statistics
     */
    const SafePointStats& stats() const { return stats_; }

private:
    bool allThreadsSafe() const;

    // Global safe-point flag
    std::atomic<bool> globalSafePointRequested_{false};
    std::atomic<bool> gcInProgress_{false};

    // Thread registry
    std::unordered_map<std::thread::id, ThreadDescriptor*> threads_;
    mutable std::mutex threadsMutex_;

    // Synchronization
    std::mutex safePointMutex_;
    std::condition_variable safePointCV_;     // Threads wait here
    std::condition_variable gcWaitCV_;        // GC waits here

    SafePointStats stats_;
};

// =============================================================================
// Safe-Point Scope Helpers
// =============================================================================

/**
 * @brief RAII: mark thread as in-native for the scope duration
 */
class NativeScope {
public:
    explicit NativeScope(SafePointManager& mgr) : mgr_(mgr) {
        mgr_.enterNative();
    }
    ~NativeScope() { mgr_.leaveNative(); }
    NativeScope(const NativeScope&) = delete;
    NativeScope& operator=(const NativeScope&) = delete;
private:
    SafePointManager& mgr_;
};

/**
 * @brief RAII: mark thread as blocked
 */
class BlockedScope {
public:
    explicit BlockedScope(SafePointManager& mgr) : mgr_(mgr) {
        mgr_.enterBlocked();
    }
    ~BlockedScope() { mgr_.leaveBlocked(); }
    BlockedScope(const BlockedScope&) = delete;
    BlockedScope& operator=(const BlockedScope&) = delete;
private:
    SafePointManager& mgr_;
};

// =============================================================================
// Implementation
// =============================================================================

inline SafePointManager::SafePointManager() = default;

inline SafePointManager::~SafePointManager() {
    std::lock_guard<std::mutex> lock(threadsMutex_);
    for (auto& [id, desc] : threads_) {
        delete desc;
    }
    threads_.clear();
}

inline void SafePointManager::registerThread(const std::string& name,
                                              void* stackBase) {
    auto* desc = new ThreadDescriptor();
    desc->id = std::this_thread::get_id();
    desc->name = name;
    desc->state = ThreadState::Running;
    desc->stackBase = stackBase;

    std::lock_guard<std::mutex> lock(threadsMutex_);
    threads_[desc->id] = desc;
}

inline void SafePointManager::unregisterThread() {
    auto id = std::this_thread::get_id();
    std::lock_guard<std::mutex> lock(threadsMutex_);
    auto it = threads_.find(id);
    if (it != threads_.end()) {
        it->second->state = ThreadState::Terminated;
        it->second->atSafePoint = true;
        gcWaitCV_.notify_all();  // GC might be waiting for this thread
        delete it->second;
        threads_.erase(it);
    }
}

inline ThreadDescriptor* SafePointManager::currentThread() {
    auto id = std::this_thread::get_id();
    std::lock_guard<std::mutex> lock(threadsMutex_);
    auto it = threads_.find(id);
    return it != threads_.end() ? it->second : nullptr;
}

inline void SafePointManager::enterSafePoint() {
    auto* desc = currentThread();
    if (!desc) return;

    // Capture stack top for root scanning
    volatile int marker;
    desc->stackTop = const_cast<int*>(&marker);
    desc->state = ThreadState::AtSafePoint;
    desc->atSafePoint = true;
    desc->safePointsReached++;

    // Notify GC that this thread is safe
    gcWaitCV_.notify_all();

    // Wait until GC is done
    {
        std::unique_lock<std::mutex> lock(safePointMutex_);
        safePointCV_.wait(lock, [this]() {
            return !gcInProgress_.load();
        });
    }

    desc->state = ThreadState::Running;
    desc->atSafePoint = false;
}

inline void SafePointManager::enterNative() {
    auto* desc = currentThread();
    if (!desc) return;
    desc->state = ThreadState::InNative;
    desc->atSafePoint = true;
    gcWaitCV_.notify_all();
}

inline void SafePointManager::leaveNative() {
    auto* desc = currentThread();
    if (!desc) return;

    // If GC is in progress, block until done
    if (gcInProgress_.load()) {
        std::unique_lock<std::mutex> lock(safePointMutex_);
        safePointCV_.wait(lock, [this]() {
            return !gcInProgress_.load();
        });
    }

    desc->state = ThreadState::Running;
    desc->atSafePoint = false;
}

inline void SafePointManager::enterBlocked() {
    auto* desc = currentThread();
    if (!desc) return;
    desc->state = ThreadState::Blocked;
    desc->atSafePoint = true;
    gcWaitCV_.notify_all();
}

inline void SafePointManager::leaveBlocked() {
    leaveNative();  // Same logic
}

inline bool SafePointManager::requestSafePoint(int64_t timeoutMs) {
    auto start = std::chrono::steady_clock::now();

    gcInProgress_ = true;
    globalSafePointRequested_ = true;
    stats_.totalRequests++;

    // Wait for all threads to be safe
    {
        std::unique_lock<std::mutex> lock(safePointMutex_);
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);

        bool success = gcWaitCV_.wait_until(lock, deadline, [this]() {
            return allThreadsSafe();
        });

        if (!success) {
            stats_.timeoutEvents++;
            gcInProgress_ = false;
            globalSafePointRequested_ = false;
            safePointCV_.notify_all();
            return false;
        }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();

    stats_.totalTimeToSafePointUs += static_cast<uint64_t>(elapsed);
    if (static_cast<uint64_t>(elapsed) > stats_.maxTimeToSafePointUs) {
        stats_.maxTimeToSafePointUs = static_cast<uint64_t>(elapsed);
    }
    stats_.avgTimeToSafePointUs = static_cast<double>(stats_.totalTimeToSafePointUs)
        / static_cast<double>(stats_.totalRequests);

    return true;
}

inline void SafePointManager::releaseSafePoint() {
    globalSafePointRequested_ = false;
    gcInProgress_ = false;
    safePointCV_.notify_all();
}

inline bool SafePointManager::withAllThreadsStopped(
    std::function<void()> callback, int64_t timeoutMs
) {
    if (!requestSafePoint(timeoutMs)) return false;
    callback();
    releaseSafePoint();
    return true;
}

inline void SafePointManager::forEachThread(
    std::function<void(ThreadDescriptor& thread)> callback
) {
    std::lock_guard<std::mutex> lock(threadsMutex_);
    for (auto& [id, desc] : threads_) {
        if (desc->state != ThreadState::Terminated) {
            callback(*desc);
        }
    }
}

inline size_t SafePointManager::threadCount() const {
    std::lock_guard<std::mutex> lock(threadsMutex_);
    return threads_.size();
}

inline bool SafePointManager::allThreadsSafe() const {
    std::lock_guard<std::mutex> lock(threadsMutex_);
    for (auto& [id, desc] : threads_) {
        if (!desc->isSafe()) return false;
    }
    return true;
}

} // namespace Zepra::Heap
