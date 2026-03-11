// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file event_loop.cpp
 * @brief Browser-grade event loop implementation
 */

#include "runtime/execution/event_loop.hpp"
#include "runtime/execution/vm.hpp"
#include "runtime/async/async.hpp"
#include <thread>
#include <algorithm>

namespace Zepra::Runtime {

// =============================================================================
// EventLoop
// =============================================================================

EventLoop::EventLoop(VM* vm) : vm_(vm) {}

uint32_t EventLoop::setTimeout(Function* callback, uint32_t delayMs) {
    uint32_t id = nextTimerId_++;
    TimerEntry entry;
    entry.id = id;
    entry.deadline = SteadyClock::now() + Milliseconds(delayMs);
    entry.callback = callback;
    entry.interval = Milliseconds(0);
    timers_.push(entry);
    return id;
}

uint32_t EventLoop::setInterval(Function* callback, uint32_t intervalMs) {
    uint32_t id = nextTimerId_++;
    TimerEntry entry;
    entry.id = id;
    entry.deadline = SteadyClock::now() + Milliseconds(intervalMs);
    entry.callback = callback;
    entry.interval = Milliseconds(intervalMs);
    timers_.push(entry);
    return id;
}

void EventLoop::clearTimer(uint32_t id) {
    // Mark-and-skip: we can't erase from a priority_queue efficiently,
    // so we rebuild without the cancelled ID. For small timer counts
    // this is fine. For large counts, use a separate cancelled-set.
    std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<TimerEntry>> filtered;
    while (!timers_.empty()) {
        TimerEntry entry = timers_.top();
        timers_.pop();
        if (entry.id != id) {
            filtered.push(entry);
        }
    }
    timers_ = std::move(filtered);
}

void EventLoop::enqueueMicrotask(std::function<void()> task) {
    microtasks_.push_back(std::move(task));
}

void EventLoop::postIOCallback(std::function<void()> callback, uint32_t priority) {
    ioCallbacks_.push_back({std::move(callback), priority});
}

// =============================================================================
// Main loop
// =============================================================================

void EventLoop::run() {
    running_.store(true);

    while (running_.load() && hasPendingWork()) {
        tick();
    }

    running_.store(false);
}

bool EventLoop::tick() {
    tickCount_++;
    bool didWork = false;

    // 1. Process expired timers
    if (processTimers()) didWork = true;

    // 2. Drain all microtasks
    drainMicrotasks();

    // 3. Process I/O completions
    processIOCallbacks();

    // 4. If no work and timers pending, sleep until next deadline
    if (!didWork && !timers_.empty() && microtasks_.empty() && ioCallbacks_.empty()) {
        auto now = SteadyClock::now();
        auto nextDeadline = timers_.top().deadline;
        if (nextDeadline > now) {
            auto sleepDuration = std::chrono::duration_cast<Milliseconds>(nextDeadline - now);
            // Cap sleep to 10ms to stay responsive
            if (sleepDuration > Milliseconds(10)) {
                sleepDuration = Milliseconds(10);
            }
            std::this_thread::sleep_for(sleepDuration);
        }
    }

    return didWork;
}

bool EventLoop::hasPendingWork() const {
    return !timers_.empty() || !microtasks_.empty() || !ioCallbacks_.empty();
}

// =============================================================================
// Internal
// =============================================================================

bool EventLoop::processTimers() {
    if (timers_.empty()) return false;

    auto now = SteadyClock::now();
    bool fired = false;

    while (!timers_.empty() && timers_.top().deadline <= now) {
        TimerEntry entry = timers_.top();
        timers_.pop();

        if (entry.cancelled) continue;

        // Execute the timer callback
        if (entry.callback && vm_) {
            try {
                vm_->executeCallback(entry.callback, Value::undefined(), {});
            } catch (...) {
                // Timer callbacks must not crash the event loop
            }
        }

        fired = true;

        // Re-enqueue for setInterval
        if (entry.interval.count() > 0) {
            entry.deadline = SteadyClock::now() + entry.interval;
            timers_.push(entry);
        }
    }

    return fired;
}

void EventLoop::drainMicrotasks() {
    // Drain the MicrotaskQueue from async.cpp as well
    MicrotaskQueue::instance().process();

    // Drain our local microtask queue
    // Note: microtasks can enqueue more microtasks, so we loop until empty
    while (!microtasks_.empty()) {
        auto batch = std::move(microtasks_);
        microtasks_.clear();

        for (auto& task : batch) {
            try {
                task();
            } catch (...) {
                // Microtask errors must not crash the loop
            }
        }

        // Also drain the global queue in case microtasks enqueued via Promise
        MicrotaskQueue::instance().process();
    }
}

void EventLoop::processIOCallbacks() {
    if (ioCallbacks_.empty()) return;

    // Sort by priority (higher = first)
    std::sort(ioCallbacks_.begin(), ioCallbacks_.end(),
        [](const IOCallback& a, const IOCallback& b) {
            return a.priority > b.priority;
        });

    auto batch = std::move(ioCallbacks_);
    ioCallbacks_.clear();

    for (auto& io : batch) {
        try {
            io.callback();
        } catch (...) {
            // I/O callbacks must not crash the loop
        }
    }
}

} // namespace Zepra::Runtime
