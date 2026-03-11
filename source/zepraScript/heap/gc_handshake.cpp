// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file gc_handshake.cpp
 * @brief Per-thread GC handshake protocol
 *
 * The handshake is a lightweight alternative to full stop-the-world:
 * instead of suspending all threads simultaneously, the GC requests
 * each thread perform an action individually (e.g. scan its own roots,
 * enable/disable a barrier, acknowledge a phase transition).
 *
 * Protocol:
 * 1. GC posts a HandshakeRequest to each thread
 * 2. Each thread executes the request at its next safe-point
 * 3. Thread signals completion
 * 4. GC waits for all threads to complete
 *
 * This avoids the thundering-herd problem of STW pauses and allows
 * per-thread root scanning to overlap with mutator execution on
 * other threads.
 *
 * Handshake types:
 * - EnableSATB: turn on SATB write barrier
 * - DisableSATB: turn off SATB write barrier
 * - ScanRoots: scan this thread's stack and handle scopes
 * - FlushSATB: drain SATB buffer to global
 * - EnableWriteBarrier: enable generational barrier
 * - AcknowledgePhase: acknowledge GC phase transition
 * - RetireTLAB: return remaining TLAB space to allocator
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
#include <cassert>
#include <memory>
#include <algorithm>
#include <deque>

namespace Zepra::Heap {

// =============================================================================
// Handshake Operation
// =============================================================================

enum class HandshakeOp : uint8_t {
    None,
    EnableSATB,
    DisableSATB,
    ScanRoots,
    FlushSATB,
    EnableWriteBarrier,
    DisableWriteBarrier,
    AcknowledgePhase,
    RetireTLAB,
    Custom,
};

static const char* handshakeOpName(HandshakeOp op) {
    switch (op) {
        case HandshakeOp::None: return "None";
        case HandshakeOp::EnableSATB: return "EnableSATB";
        case HandshakeOp::DisableSATB: return "DisableSATB";
        case HandshakeOp::ScanRoots: return "ScanRoots";
        case HandshakeOp::FlushSATB: return "FlushSATB";
        case HandshakeOp::EnableWriteBarrier: return "EnableWriteBarrier";
        case HandshakeOp::DisableWriteBarrier: return "DisableWriteBarrier";
        case HandshakeOp::AcknowledgePhase: return "AcknowledgePhase";
        case HandshakeOp::RetireTLAB: return "RetireTLAB";
        case HandshakeOp::Custom: return "Custom";
        default: return "Unknown";
    }
}

// =============================================================================
// Handshake Request
// =============================================================================

struct HandshakeRequest {
    uint64_t requestId;
    HandshakeOp operation;
    uint64_t targetThreadId;    // 0 = all threads

    // For ScanRoots: visitor callback
    std::function<void(void** slot)> rootVisitor;

    // For Custom: arbitrary callback
    std::function<void(uint64_t threadId)> customAction;

    // Completion tracking
    std::atomic<bool> completed{false};
    std::atomic<uint32_t> pendingThreads{0};
    uint64_t issuedAtUs;
    uint64_t completedAtUs;

    HandshakeRequest()
        : requestId(0)
        , operation(HandshakeOp::None)
        , targetThreadId(0)
        , issuedAtUs(0)
        , completedAtUs(0) {}
};

// =============================================================================
// Per-Thread Handshake State
// =============================================================================

struct ThreadHandshakeState {
    uint64_t threadId;

    // Pending request (GC writes, mutator reads+executes)
    std::atomic<HandshakeRequest*> pendingRequest{nullptr};

    // Flag: thread has a handshake to process
    std::atomic<bool> handshakePending{false};

    // Statistics
    uint64_t handshakesProcessed;
    double totalHandshakeMs;

    ThreadHandshakeState()
        : threadId(0)
        , handshakesProcessed(0)
        , totalHandshakeMs(0) {}
};

// =============================================================================
// Handshake Manager
// =============================================================================

class GCHandshake {
public:
    struct Config {
        uint64_t handshakeTimeoutUs;
        bool enableParallelHandshake;

        Config()
            : handshakeTimeoutUs(10000000)  // 10 seconds
            , enableParallelHandshake(true) {}
    };

    struct Stats {
        uint64_t totalHandshakes;
        uint64_t timeouts;
        double avgLatencyUs;
        double maxLatencyUs;
    };

    // Callbacks for handshake operations
    struct Callbacks {
        // Enable/disable SATB barrier on a thread
        std::function<void(uint64_t threadId, bool enable)> setSATBBarrier;

        // Enable/disable generational write barrier
        std::function<void(uint64_t threadId, bool enable)> setWriteBarrier;

        // Scan a thread's roots (stack, handles)
        std::function<void(uint64_t threadId,
            std::function<void(void** slot)>)> scanThreadRoots;

        // Flush a thread's SATB buffer
        std::function<void(uint64_t threadId)> flushSATBBuffer;

        // Retire a thread's TLAB
        std::function<void(uint64_t threadId)> retireTLAB;
    };

    explicit GCHandshake(const Config& config = Config{});
    ~GCHandshake();

    GCHandshake(const GCHandshake&) = delete;
    GCHandshake& operator=(const GCHandshake&) = delete;

    void setCallbacks(Callbacks callbacks) { cb_ = std::move(callbacks); }

    // -------------------------------------------------------------------------
    // Thread registration
    // -------------------------------------------------------------------------

    void registerThread(uint64_t threadId);
    void deregisterThread(uint64_t threadId);

    // -------------------------------------------------------------------------
    // Initiate handshakes (GC side)
    // -------------------------------------------------------------------------

    /**
     * @brief Issue a handshake to all threads
     * Blocks until all threads have processed the request.
     * @return true if all completed within timeout
     */
    bool issueAll(HandshakeOp op);

    /**
     * @brief Issue a handshake to a specific thread
     */
    bool issueOne(uint64_t threadId, HandshakeOp op);

    /**
     * @brief Issue scan-roots handshake with visitor
     */
    bool issueScanRoots(std::function<void(void** slot)> visitor);

    /**
     * @brief Issue a custom action to all threads
     */
    bool issueCustom(std::function<void(uint64_t threadId)> action);

    // -------------------------------------------------------------------------
    // Process handshakes (mutator side, called at safe-points)
    // -------------------------------------------------------------------------

    /**
     * @brief Check and process pending handshake for current thread
     * Called from the mutator at safe-point polls.
     */
    void processPending(uint64_t threadId);

    /**
     * @brief Check if thread has pending handshake
     */
    bool hasPending(uint64_t threadId) const;

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    Stats computeStats() const;

private:
    void executeHandshake(uint64_t threadId, HandshakeRequest& req);
    ThreadHandshakeState* findState(uint64_t threadId);
    const ThreadHandshakeState* findState(uint64_t threadId) const;

    Config config_;
    Callbacks cb_;

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<ThreadHandshakeState>> threadStates_;

    // Request tracking
    std::atomic<uint64_t> nextRequestId_{1};
    std::deque<std::unique_ptr<HandshakeRequest>> activeRequests_;

    // Stats
    std::atomic<uint64_t> totalHandshakes_{0};
    std::atomic<uint64_t> timeouts_{0};
    double totalLatencyUs_ = 0;
    double maxLatencyUs_ = 0;
};

// =============================================================================
// Implementation
// =============================================================================

inline GCHandshake::GCHandshake(const Config& config)
    : config_(config) {}

inline GCHandshake::~GCHandshake() = default;

inline void GCHandshake::registerThread(uint64_t threadId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto state = std::make_unique<ThreadHandshakeState>();
    state->threadId = threadId;
    threadStates_.push_back(std::move(state));
}

inline void GCHandshake::deregisterThread(uint64_t threadId) {
    std::lock_guard<std::mutex> lock(mutex_);
    threadStates_.erase(
        std::remove_if(threadStates_.begin(), threadStates_.end(),
            [threadId](const auto& s) { return s->threadId == threadId; }),
        threadStates_.end());
}

inline bool GCHandshake::issueAll(HandshakeOp op) {
    auto req = std::make_unique<HandshakeRequest>();
    req->requestId = nextRequestId_.fetch_add(1, std::memory_order_relaxed);
    req->operation = op;
    req->targetThreadId = 0;  // All threads
    req->issuedAtUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    HandshakeRequest* rawReq = req.get();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t pending = 0;
        for (auto& state : threadStates_) {
            state->pendingRequest.store(rawReq, std::memory_order_release);
            state->handshakePending.store(true, std::memory_order_release);
            pending++;
        }
        rawReq->pendingThreads.store(pending, std::memory_order_release);
        activeRequests_.push_back(std::move(req));
    }

    // Wait for completion
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::microseconds(config_.handshakeTimeoutUs);

    while (std::chrono::steady_clock::now() < deadline) {
        if (rawReq->pendingThreads.load(std::memory_order_acquire) == 0) {
            rawReq->completed.store(true, std::memory_order_release);
            rawReq->completedAtUs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());

            double latencyUs = static_cast<double>(
                rawReq->completedAtUs - rawReq->issuedAtUs);
            totalLatencyUs_ += latencyUs;
            if (latencyUs > maxLatencyUs_) maxLatencyUs_ = latencyUs;
            totalHandshakes_.fetch_add(1, std::memory_order_relaxed);

            return true;
        }
        std::this_thread::yield();
    }

    timeouts_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

inline bool GCHandshake::issueOne(uint64_t threadId, HandshakeOp op) {
    auto req = std::make_unique<HandshakeRequest>();
    req->requestId = nextRequestId_.fetch_add(1, std::memory_order_relaxed);
    req->operation = op;
    req->targetThreadId = threadId;
    req->pendingThreads.store(1, std::memory_order_relaxed);
    req->issuedAtUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    HandshakeRequest* rawReq = req.get();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* state = findState(threadId);
        if (!state) return false;
        state->pendingRequest.store(rawReq, std::memory_order_release);
        state->handshakePending.store(true, std::memory_order_release);
        activeRequests_.push_back(std::move(req));
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::microseconds(config_.handshakeTimeoutUs);

    while (std::chrono::steady_clock::now() < deadline) {
        if (rawReq->pendingThreads.load(std::memory_order_acquire) == 0) {
            rawReq->completed.store(true, std::memory_order_release);
            totalHandshakes_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        std::this_thread::yield();
    }

    timeouts_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

inline bool GCHandshake::issueScanRoots(
    std::function<void(void** slot)> visitor
) {
    auto req = std::make_unique<HandshakeRequest>();
    req->requestId = nextRequestId_.fetch_add(1, std::memory_order_relaxed);
    req->operation = HandshakeOp::ScanRoots;
    req->rootVisitor = std::move(visitor);
    req->issuedAtUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    HandshakeRequest* rawReq = req.get();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t pending = 0;
        for (auto& state : threadStates_) {
            state->pendingRequest.store(rawReq, std::memory_order_release);
            state->handshakePending.store(true, std::memory_order_release);
            pending++;
        }
        rawReq->pendingThreads.store(pending, std::memory_order_release);
        activeRequests_.push_back(std::move(req));
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::microseconds(config_.handshakeTimeoutUs);

    while (std::chrono::steady_clock::now() < deadline) {
        if (rawReq->pendingThreads.load(std::memory_order_acquire) == 0) {
            rawReq->completed.store(true, std::memory_order_release);
            totalHandshakes_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        std::this_thread::yield();
    }

    timeouts_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

inline bool GCHandshake::issueCustom(
    std::function<void(uint64_t threadId)> action
) {
    auto req = std::make_unique<HandshakeRequest>();
    req->requestId = nextRequestId_.fetch_add(1, std::memory_order_relaxed);
    req->operation = HandshakeOp::Custom;
    req->customAction = std::move(action);
    req->issuedAtUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    HandshakeRequest* rawReq = req.get();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t pending = 0;
        for (auto& state : threadStates_) {
            state->pendingRequest.store(rawReq, std::memory_order_release);
            state->handshakePending.store(true, std::memory_order_release);
            pending++;
        }
        rawReq->pendingThreads.store(pending, std::memory_order_release);
        activeRequests_.push_back(std::move(req));
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::microseconds(config_.handshakeTimeoutUs);

    while (std::chrono::steady_clock::now() < deadline) {
        if (rawReq->pendingThreads.load(std::memory_order_acquire) == 0) {
            rawReq->completed.store(true, std::memory_order_release);
            totalHandshakes_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        std::this_thread::yield();
    }

    timeouts_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

inline void GCHandshake::processPending(uint64_t threadId) {
    ThreadHandshakeState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state = findState(threadId);
    }
    if (!state) return;
    if (!state->handshakePending.load(std::memory_order_acquire)) return;

    HandshakeRequest* req = state->pendingRequest.load(std::memory_order_acquire);
    if (!req) return;

    executeHandshake(threadId, *req);

    state->pendingRequest.store(nullptr, std::memory_order_release);
    state->handshakePending.store(false, std::memory_order_release);
    state->handshakesProcessed++;

    req->pendingThreads.fetch_sub(1, std::memory_order_release);
}

inline bool GCHandshake::hasPending(uint64_t threadId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto* state = findState(threadId);
    if (!state) return false;
    return state->handshakePending.load(std::memory_order_acquire);
}

inline void GCHandshake::executeHandshake(
    uint64_t threadId, HandshakeRequest& req
) {
    switch (req.operation) {
        case HandshakeOp::EnableSATB:
            if (cb_.setSATBBarrier) cb_.setSATBBarrier(threadId, true);
            break;
        case HandshakeOp::DisableSATB:
            if (cb_.setSATBBarrier) cb_.setSATBBarrier(threadId, false);
            break;
        case HandshakeOp::ScanRoots:
            if (cb_.scanThreadRoots && req.rootVisitor) {
                cb_.scanThreadRoots(threadId, req.rootVisitor);
            }
            break;
        case HandshakeOp::FlushSATB:
            if (cb_.flushSATBBuffer) cb_.flushSATBBuffer(threadId);
            break;
        case HandshakeOp::EnableWriteBarrier:
            if (cb_.setWriteBarrier) cb_.setWriteBarrier(threadId, true);
            break;
        case HandshakeOp::DisableWriteBarrier:
            if (cb_.setWriteBarrier) cb_.setWriteBarrier(threadId, false);
            break;
        case HandshakeOp::RetireTLAB:
            if (cb_.retireTLAB) cb_.retireTLAB(threadId);
            break;
        case HandshakeOp::Custom:
            if (req.customAction) req.customAction(threadId);
            break;
        case HandshakeOp::AcknowledgePhase:
        case HandshakeOp::None:
            break;
    }
}

inline GCHandshake::Stats GCHandshake::computeStats() const {
    Stats stats;
    stats.totalHandshakes = totalHandshakes_.load(std::memory_order_relaxed);
    stats.timeouts = timeouts_.load(std::memory_order_relaxed);
    stats.maxLatencyUs = maxLatencyUs_;
    stats.avgLatencyUs = stats.totalHandshakes > 0
        ? totalLatencyUs_ / static_cast<double>(stats.totalHandshakes) : 0;
    return stats;
}

inline ThreadHandshakeState* GCHandshake::findState(uint64_t threadId) {
    for (auto& s : threadStates_) {
        if (s->threadId == threadId) return s.get();
    }
    return nullptr;
}

inline const ThreadHandshakeState* GCHandshake::findState(
    uint64_t threadId
) const {
    for (const auto& s : threadStates_) {
        if (s->threadId == threadId) return s.get();
    }
    return nullptr;
}

} // namespace Zepra::Heap
