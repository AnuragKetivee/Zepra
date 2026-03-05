/**
 * @file gc_pipeline_orchestrator.cpp
 * @brief Central GC pipeline coordinator
 *
 * Orchestrates the full GC lifecycle across all subsystems:
 *
 *  ┌──────────────────────────────────────────────────┐
 *  │  Trigger (allocation failure / scheduling)       │
 *  └──────────────────┬───────────────────────────────┘
 *                     ↓
 *  ┌──────────────────────────────────────────────────┐
 *  │  Phase 1: Prepare                                │
 *  │  - Decide GC type (minor/major/full)             │
 *  │  - Request safe-point stop                       │
 *  │  - Snapshot roots                                │
 *  └──────────────────┬───────────────────────────────┘
 *                     ↓
 *  ┌──────────────────────────────────────────────────┐
 *  │  Phase 2: Mark                                   │
 *  │  - Initial mark (STW, roots only)                │
 *  │  - Concurrent mark (parallel workers)            │
 *  │  - Remark (STW, SATB + dirty cards)              │
 *  └──────────────────┬───────────────────────────────┘
 *                     ↓
 *  ┌──────────────────────────────────────────────────┐
 *  │  Phase 3: Weak Processing                        │
 *  │  - Clear dead WeakRefs                           │
 *  │  - Ephemeron fixpoint                            │
 *  │  - FinalizationRegistry cleanup                  │
 *  │  - Weak handle callbacks                         │
 *  └──────────────────┬───────────────────────────────┘
 *                     ↓
 *  ┌──────────────────────────────────────────────────┐
 *  │  Phase 4: Sweep / Compact                        │
 *  │  - Concurrent sweep (reclaim dead objects)        │
 *  │  - OR: Compact (evacuate sparse regions)          │
 *  │  - JIT code GC (collect dead compiled code)       │
 *  └──────────────────┬───────────────────────────────┘
 *                     ↓
 *  ┌──────────────────────────────────────────────────┐
 *  │  Phase 5: Complete                               │
 *  │  - Update heap statistics                        │
 *  │  - Resize heap if needed                         │
 *  │  - Schedule next GC                              │
 *  │  - Fire finalizer callbacks                      │
 *  │  - Resume mutator threads                        │
 *  └──────────────────────────────────────────────────┘
 */

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <deque>
#include <functional>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <memory>
#include <algorithm>

namespace Zepra::Heap {

// =============================================================================
// GC Types and Phases
// =============================================================================

enum class GCType : uint8_t {
    Minor,      // Young generation only (scavenge)
    Major,      // Full heap concurrent mark-sweep
    Full,       // Stop-the-world full GC (emergency)
    Compact,    // Major + compaction (defragmentation)
};

enum class GCPhase : uint8_t {
    Idle,
    Prepare,
    InitialMark,
    ConcurrentMark,
    Remark,
    WeakProcessing,
    Sweep,
    Compact,
    Complete,
};

static const char* gcTypeName(GCType type) {
    switch (type) {
        case GCType::Minor: return "Minor";
        case GCType::Major: return "Major";
        case GCType::Full: return "Full";
        case GCType::Compact: return "Compact";
    }
    return "Unknown";
}

static const char* gcPhaseName(GCPhase phase) {
    switch (phase) {
        case GCPhase::Idle: return "Idle";
        case GCPhase::Prepare: return "Prepare";
        case GCPhase::InitialMark: return "InitialMark";
        case GCPhase::ConcurrentMark: return "ConcurrentMark";
        case GCPhase::Remark: return "Remark";
        case GCPhase::WeakProcessing: return "WeakProcessing";
        case GCPhase::Sweep: return "Sweep";
        case GCPhase::Compact: return "Compact";
        case GCPhase::Complete: return "Complete";
    }
    return "Unknown";
}

// =============================================================================
// GC Trigger
// =============================================================================

enum class GCTrigger : uint8_t {
    AllocationFailure,      // No space to allocate
    ThresholdReached,       // Heap usage hit threshold
    Scheduled,              // Scheduler decided it's time
    MemoryPressure,         // OS reported memory pressure
    UserRequested,          // Explicit GC() call
    Emergency,              // OOM imminent
};

// =============================================================================
// GC Cycle Result
// =============================================================================

struct GCCycleResult {
    uint64_t cycleId;
    GCType type;
    GCTrigger trigger;

    // Timing
    double totalMs;
    double prepareMs;
    double initialMarkMs;
    double concurrentMarkMs;
    double remarkMs;
    double weakProcessingMs;
    double sweepMs;
    double compactMs;
    double completeMs;

    // STW pauses
    double totalStwMs;       // Total stop-the-world time
    size_t stwPauseCount;

    // Work
    size_t objectsMarked;
    size_t objectsSwept;
    size_t objectsEvacuated;
    size_t bytesReclaimed;
    size_t bytesPromoted;

    // Weak processing
    size_t weakRefsCleared;
    size_t finalizersRun;
    size_t ephemeronIterations;

    // Code GC
    size_t codeBlocksCollected;

    // Heap state after GC
    size_t heapUsedAfter;
    size_t heapCapacityAfter;
    double heapOccupancyAfter;
};

// =============================================================================
// Pipeline Callbacks
// =============================================================================

struct PipelineCallbacks {
    // Thread control
    std::function<void()> requestSafePointStop;
    std::function<void()> resumeThreads;
    std::function<bool()> areAllThreadsStopped;

    // Root enumeration
    std::function<void(std::function<void(void** slot)>)> enumerateRoots;

    // Marking
    std::function<size_t(std::function<void(void** slot)>)> initialMark;
    std::function<size_t()> concurrentMark;
    std::function<size_t()> remark;

    // Weak processing
    std::function<size_t()> processWeakRefs;
    std::function<size_t()> processEphemerons;
    std::function<size_t()> processFinalizationRegistries;

    // Sweep / compact
    std::function<size_t()> sweep;
    std::function<size_t()> compact;
    std::function<size_t()> collectDeadCode;

    // Scavenge (minor GC)
    std::function<size_t()> scavenge;

    // Heap info
    std::function<size_t()> heapUsed;
    std::function<size_t()> heapCapacity;
    std::function<void(size_t newSize)> resizeHeap;

    // Scheduling
    std::function<void(const GCCycleResult&)> reportCycleComplete;
};

// =============================================================================
// GC Pipeline Orchestrator
// =============================================================================

class GCPipelineOrchestrator {
public:
    struct Config {
        size_t concurrentMarkWorkers;
        size_t sweepWorkers;
        bool enableCompaction;
        double compactionTriggerFragmentation;
        bool verbose;

        Config()
            : concurrentMarkWorkers(2)
            , sweepWorkers(2)
            , enableCompaction(true)
            , compactionTriggerFragmentation(0.30)
            , verbose(false) {}
    };

    explicit GCPipelineOrchestrator(const Config& config = Config{})
        : config_(config)
        , currentPhase_(GCPhase::Idle)
        , cycleCount_(0)
        , gcActive_(false) {}

    void setCallbacks(PipelineCallbacks cb) { cb_ = std::move(cb); }

    /**
     * @brief Check if GC is currently running
     */
    bool isActive() const {
        return gcActive_.load(std::memory_order_acquire);
    }

    GCPhase currentPhase() const { return currentPhase_; }

    /**
     * @brief Run a minor GC cycle (young generation scavenge)
     */
    GCCycleResult runMinorGC(GCTrigger trigger) {
        return runCycle(GCType::Minor, trigger);
    }

    /**
     * @brief Run a major GC cycle (concurrent mark-sweep)
     */
    GCCycleResult runMajorGC(GCTrigger trigger) {
        return runCycle(GCType::Major, trigger);
    }

    /**
     * @brief Run full GC (stop-the-world, emergency)
     */
    GCCycleResult runFullGC(GCTrigger trigger) {
        return runCycle(GCType::Full, trigger);
    }

    /**
     * @brief Run compacting GC
     */
    GCCycleResult runCompactGC(GCTrigger trigger) {
        return runCycle(GCType::Compact, trigger);
    }

    const GCCycleResult& lastResult() const { return lastResult_; }
    uint64_t cycleCount() const { return cycleCount_; }

private:
    GCCycleResult runCycle(GCType type, GCTrigger trigger) {
        GCCycleResult result{};
        result.cycleId = ++cycleCount_;
        result.type = type;
        result.trigger = trigger;

        gcActive_.store(true, std::memory_order_release);

        auto cycleStart = std::chrono::steady_clock::now();

        if (config_.verbose) {
            fprintf(stderr, "[gc] Cycle #%lu: %s trigger=%d\n",
                static_cast<unsigned long>(result.cycleId),
                gcTypeName(type), static_cast<int>(trigger));
        }

        if (type == GCType::Minor) {
            runMinorCycle(result);
        } else {
            runMajorCycle(result, type == GCType::Compact ||
                (config_.enableCompaction && shouldCompact()));
        }

        // Phase: Complete
        setPhase(GCPhase::Complete);
        auto completeStart = now();

        if (cb_.heapUsed) result.heapUsedAfter = cb_.heapUsed();
        if (cb_.heapCapacity) result.heapCapacityAfter = cb_.heapCapacity();
        if (result.heapCapacityAfter > 0) {
            result.heapOccupancyAfter =
                static_cast<double>(result.heapUsedAfter) /
                static_cast<double>(result.heapCapacityAfter);
        }

        if (cb_.reportCycleComplete) {
            cb_.reportCycleComplete(result);
        }

        result.completeMs = elapsedMs(completeStart);
        result.totalMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - cycleStart).count();

        setPhase(GCPhase::Idle);
        gcActive_.store(false, std::memory_order_release);

        lastResult_ = result;

        if (config_.verbose) {
            fprintf(stderr,
                "[gc] Cycle #%lu complete: %s %.1fms "
                "(STW %.1fms) reclaimed %zuKB\n",
                static_cast<unsigned long>(result.cycleId),
                gcTypeName(type), result.totalMs,
                result.totalStwMs,
                result.bytesReclaimed / 1024);
        }

        return result;
    }

    void runMinorCycle(GCCycleResult& result) {
        // STW pause for scavenge
        setPhase(GCPhase::Prepare);
        auto prepStart = now();

        if (cb_.requestSafePointStop) cb_.requestSafePointStop();
        result.prepareMs = elapsedMs(prepStart);

        auto stwStart = now();

        setPhase(GCPhase::Sweep);  // Scavenge is like sweep for nursery
        if (cb_.scavenge) {
            result.objectsEvacuated = cb_.scavenge();
        }

        // Resume
        if (cb_.resumeThreads) cb_.resumeThreads();
        result.totalStwMs = elapsedMs(stwStart);
        result.stwPauseCount = 1;
    }

    void runMajorCycle(GCCycleResult& result, bool doCompact) {
        // Phase 1: Prepare
        setPhase(GCPhase::Prepare);
        auto prepStart = now();
        result.prepareMs = elapsedMs(prepStart);

        // Phase 2a: Initial Mark (STW)
        setPhase(GCPhase::InitialMark);
        auto imStart = now();

        if (cb_.requestSafePointStop) cb_.requestSafePointStop();
        auto stwStart1 = now();

        if (cb_.initialMark) {
            result.objectsMarked = cb_.initialMark(
                [](void** slot) { (void)slot; /* root visitor */ });
        }

        if (cb_.resumeThreads) cb_.resumeThreads();
        double stw1 = elapsedMs(stwStart1);
        result.totalStwMs += stw1;
        result.stwPauseCount++;
        result.initialMarkMs = elapsedMs(imStart);

        // Phase 2b: Concurrent Mark
        setPhase(GCPhase::ConcurrentMark);
        auto cmStart = now();

        if (cb_.concurrentMark) {
            result.objectsMarked += cb_.concurrentMark();
        }

        result.concurrentMarkMs = elapsedMs(cmStart);

        // Phase 2c: Remark (STW)
        setPhase(GCPhase::Remark);
        auto rmStart = now();

        if (cb_.requestSafePointStop) cb_.requestSafePointStop();
        auto stwStart2 = now();

        if (cb_.remark) {
            result.objectsMarked += cb_.remark();
        }

        if (cb_.resumeThreads) cb_.resumeThreads();
        double stw2 = elapsedMs(stwStart2);
        result.totalStwMs += stw2;
        result.stwPauseCount++;
        result.remarkMs = elapsedMs(rmStart);

        // Phase 3: Weak Processing
        setPhase(GCPhase::WeakProcessing);
        auto wpStart = now();

        if (cb_.processWeakRefs) {
            result.weakRefsCleared = cb_.processWeakRefs();
        }
        if (cb_.processEphemerons) {
            result.ephemeronIterations = cb_.processEphemerons();
        }
        if (cb_.processFinalizationRegistries) {
            result.finalizersRun = cb_.processFinalizationRegistries();
        }

        result.weakProcessingMs = elapsedMs(wpStart);

        // Phase 4: Sweep or Compact
        if (doCompact) {
            setPhase(GCPhase::Compact);
            auto compStart = now();

            if (cb_.compact) {
                result.objectsEvacuated = cb_.compact();
            }

            result.compactMs = elapsedMs(compStart);
        } else {
            setPhase(GCPhase::Sweep);
            auto swStart = now();

            if (cb_.sweep) {
                result.bytesReclaimed = cb_.sweep();
            }

            result.sweepMs = elapsedMs(swStart);
        }

        // Code GC
        if (cb_.collectDeadCode) {
            result.codeBlocksCollected = cb_.collectDeadCode();
        }
    }

    bool shouldCompact() const {
        // Heuristic: compact if fragmentation exceeds threshold
        if (!cb_.heapUsed || !cb_.heapCapacity) return false;
        size_t used = cb_.heapUsed();
        size_t capacity = cb_.heapCapacity();
        if (capacity == 0) return false;
        double frag = 1.0 - static_cast<double>(used) /
                             static_cast<double>(capacity);
        return frag > config_.compactionTriggerFragmentation;
    }

    void setPhase(GCPhase phase) {
        currentPhase_ = phase;
    }

    static std::chrono::steady_clock::time_point now() {
        return std::chrono::steady_clock::now();
    }

    static double elapsedMs(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
    }

    Config config_;
    PipelineCallbacks cb_;
    GCPhase currentPhase_;
    uint64_t cycleCount_;
    std::atomic<bool> gcActive_;
    GCCycleResult lastResult_;
};

} // namespace Zepra::Heap
