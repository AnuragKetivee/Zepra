// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file event_loop.hpp
 * @brief Browser-grade event loop with timer queue, microtask drain, and I/O
 *
 * Single-threaded event loop following the HTML spec processing model:
 *   1. Pick oldest timer/task whose deadline has passed
 *   2. Run it
 *   3. Drain ALL pending microtasks
 *   4. Process I/O completion callbacks
 *   5. If idle, sleep until next deadline or I/O
 */

#pragma once

#include "runtime/objects/value.hpp"
#include "runtime/objects/function.hpp"
#include <cstdint>
#include <vector>
#include <queue>
#include <functional>
#include <chrono>
#include <atomic>

namespace Zepra::Runtime {

class VM;
class Context;

using SteadyClock = std::chrono::steady_clock;
using TimePoint   = SteadyClock::time_point;
using Duration    = SteadyClock::duration;
using Milliseconds = std::chrono::milliseconds;

// =============================================================================
// Timer entry
// =============================================================================

struct TimerEntry {
    uint32_t id;
    TimePoint deadline;
    Function* callback;
    Milliseconds interval;  // 0 for setTimeout, >0 for setInterval
    bool cancelled = false;

    bool operator>(const TimerEntry& other) const {
        return deadline > other.deadline;
    }
};

// =============================================================================
// I/O completion callback (fetch, WebSocket, file read, etc.)
// =============================================================================

struct IOCallback {
    std::function<void()> callback;
    uint32_t priority = 0;  // 0 = normal, higher = sooner
};

// =============================================================================
// EventLoop
// =============================================================================

class EventLoop {
public:
    explicit EventLoop(VM* vm);

    // Timer API (wired from global setTimeout/setInterval)
    uint32_t setTimeout(Function* callback, uint32_t delayMs);
    uint32_t setInterval(Function* callback, uint32_t intervalMs);
    void clearTimer(uint32_t id);

    // Microtask queue (wired from MicrotaskQueue)
    void enqueueMicrotask(std::function<void()> task);

    // I/O completion (wired from browser fetch, WebSocket, etc.)
    void postIOCallback(std::function<void()> callback, uint32_t priority = 0);

    // Run the event loop until no more work
    void run();

    // Run a single tick (for browser integration — returns true if did work)
    bool tick();

    // Check if there's pending work
    bool hasPendingWork() const;

    // Request shutdown
    void requestStop() { running_.store(false); }
    bool isRunning() const { return running_.load(); }

    // Stats
    uint64_t tickCount() const { return tickCount_; }
    uint32_t pendingTimers() const { return static_cast<uint32_t>(timers_.size()); }
    uint32_t pendingMicrotasks() const { return static_cast<uint32_t>(microtasks_.size()); }

private:
    // Process one expired timer (returns true if one fired)
    bool processTimers();

    // Drain all microtasks (called after each task)
    void drainMicrotasks();

    // Process I/O callbacks
    void processIOCallbacks();

    VM* vm_;
    uint32_t nextTimerId_ = 1;

    // Min-heap: earliest deadline on top
    std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<TimerEntry>> timers_;

    // Microtask FIFO
    std::vector<std::function<void()>> microtasks_;

    // I/O completion callbacks
    std::vector<IOCallback> ioCallbacks_;

    std::atomic<bool> running_{false};
    uint64_t tickCount_ = 0;
};

} // namespace Zepra::Runtime
