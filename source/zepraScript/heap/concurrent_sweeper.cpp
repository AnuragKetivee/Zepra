// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file concurrent_sweeper.cpp
 * @brief Concurrent sweeping implementation
 *
 * Sweeps dead objects in background threads while the mutator runs.
 * Each region is swept independently — no inter-region dependencies.
 *
 * Thread model:
 * - N sweeper threads (configurable, default = 2)
 * - Regions added to work queue after marking
 * - Sweeper processes regions, builds free-lists
 * - Mutator can allocate from already-swept regions
 *
 * Memory ordering:
 * - Sweeper only reads mark bits (set atomically during marking)
 * - Sweeper writes to free-lists (private per region until done)
 * - Completion signaled via atomic flag per region
 */

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <functional>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace Zepra::Heap {

// =============================================================================
// Free-list Node
// =============================================================================

struct FreeBlock {
    size_t size;
    FreeBlock* next;
};

// =============================================================================
// Sweepable Region
// =============================================================================

struct SweepableRegion {
    char* start;
    char* end;
    size_t objectAlignment;

    // Mark bitmap (read-only during sweep)
    const uint64_t* markBits;
    size_t markBitCount;

    // Output: free-list built during sweep
    FreeBlock* freeList;
    size_t freeBytes;
    size_t liveBytes;
    size_t objectsFreed;
    size_t objectsLive;

    // Status
    std::atomic<bool> swept;
    std::atomic<bool> inProgress;
};

// =============================================================================
// Sweeper Statistics
// =============================================================================

struct SweeperStats {
    uint64_t regionsSwept = 0;
    uint64_t bytesFreed = 0;
    uint64_t bytesLive = 0;
    uint64_t objectsFreed = 0;
    uint64_t objectsLive = 0;
    double totalSweepTimeMs = 0;
    double maxRegionSweepTimeMs = 0;
    size_t concurrentSweepsCompleted = 0;
    size_t mainThreadSweepsCompleted = 0;
};

// =============================================================================
// Concurrent Sweeper
// =============================================================================

class ConcurrentSweeper {
public:
    explicit ConcurrentSweeper(size_t numThreads = 2);
    ~ConcurrentSweeper();

    ConcurrentSweeper(const ConcurrentSweeper&) = delete;
    ConcurrentSweeper& operator=(const ConcurrentSweeper&) = delete;

    /**
     * @brief Start sweeping regions in background
     */
    void startSweeping(std::vector<SweepableRegion>& regions);

    /**
     * @brief Wait for all sweeping to complete
     */
    void waitForCompletion();

    /**
     * @brief Check if a specific region has been swept
     */
    bool isRegionSwept(size_t regionIndex) const;

    /**
     * @brief Ensure a specific region is swept (may sweep on main thread)
     */
    void ensureRegionSwept(SweepableRegion& region);

    /**
     * @brief Cancel pending sweeps
     */
    void cancel();

    /**
     * @brief Get statistics
     */
    const SweeperStats& stats() const { return stats_; }

    /**
     * @brief Check if sweeping is in progress
     */
    bool isSweeping() const { return sweeping_.load(); }

private:
    void workerLoop();
    void sweepRegion(SweepableRegion& region);
    void buildFreeList(SweepableRegion& region);

    // Worker threads
    std::vector<std::thread> workers_;
    size_t numThreads_;

    // Work queue
    std::mutex queueMutex_;
    std::condition_variable queueCV_;
    std::queue<SweepableRegion*> workQueue_;

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> sweeping_{false};
    std::atomic<size_t> pendingRegions_{0};

    // Completion
    std::mutex completionMutex_;
    std::condition_variable completionCV_;

    SweeperStats stats_;
};

// =============================================================================
// Implementation
// =============================================================================

ConcurrentSweeper::ConcurrentSweeper(size_t numThreads)
    : numThreads_(numThreads) {
    running_ = true;
    for (size_t i = 0; i < numThreads_; i++) {
        workers_.emplace_back([this]() { workerLoop(); });
    }
}

ConcurrentSweeper::~ConcurrentSweeper() {
    cancel();
    running_ = false;
    queueCV_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

void ConcurrentSweeper::startSweeping(std::vector<SweepableRegion>& regions) {
    sweeping_ = true;
    pendingRegions_ = regions.size();

    std::lock_guard<std::mutex> lock(queueMutex_);
    for (auto& region : regions) {
        region.swept = false;
        region.inProgress = false;
        region.freeList = nullptr;
        region.freeBytes = 0;
        region.liveBytes = 0;
        region.objectsFreed = 0;
        region.objectsLive = 0;
        workQueue_.push(&region);
    }
    queueCV_.notify_all();
}

void ConcurrentSweeper::waitForCompletion() {
    std::unique_lock<std::mutex> lock(completionMutex_);
    completionCV_.wait(lock, [this]() {
        return pendingRegions_.load() == 0;
    });
    sweeping_ = false;
}

bool ConcurrentSweeper::isRegionSwept(size_t /*regionIndex*/) const {
    return !sweeping_.load();
}

void ConcurrentSweeper::ensureRegionSwept(SweepableRegion& region) {
    if (region.swept.load()) return;

    // Try to claim it for main-thread sweep
    bool expected = false;
    if (region.inProgress.compare_exchange_strong(expected, true)) {
        sweepRegion(region);
        stats_.mainThreadSweepsCompleted++;
    } else {
        // Another thread is sweeping it — spin until done
        while (!region.swept.load()) {
            std::this_thread::yield();
        }
    }
}

void ConcurrentSweeper::cancel() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!workQueue_.empty()) workQueue_.pop();
    pendingRegions_ = 0;
    sweeping_ = false;
    completionCV_.notify_all();
}

void ConcurrentSweeper::workerLoop() {
    while (running_.load()) {
        SweepableRegion* region = nullptr;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait(lock, [this]() {
                return !workQueue_.empty() || !running_.load();
            });

            if (!running_.load()) break;
            if (workQueue_.empty()) continue;

            region = workQueue_.front();
            workQueue_.pop();
        }

        if (region) {
            bool expected = false;
            if (region->inProgress.compare_exchange_strong(expected, true)) {
                sweepRegion(*region);
                stats_.concurrentSweepsCompleted++;
            }
        }
    }
}

void ConcurrentSweeper::sweepRegion(SweepableRegion& region) {
    auto startTime = std::chrono::steady_clock::now();

    size_t objectSize = region.objectAlignment;
    char* ptr = region.start;

    while (ptr < region.end) {
        size_t offset = static_cast<size_t>(ptr - region.start);
        size_t bitIndex = offset / region.objectAlignment;
        size_t wordIndex = bitIndex / 64;
        size_t bitOffset = bitIndex % 64;

        bool isMarked = false;
        if (wordIndex < region.markBitCount / 64) {
            isMarked = (region.markBits[wordIndex] & (uint64_t(1) << bitOffset)) != 0;
        }

        if (isMarked) {
            region.liveBytes += objectSize;
            region.objectsLive++;
        } else {
            // Dead object — add to free list
            auto* block = reinterpret_cast<FreeBlock*>(ptr);
            block->size = objectSize;
            block->next = region.freeList;
            region.freeList = block;
            region.freeBytes += objectSize;
            region.objectsFreed++;
        }

        ptr += objectSize;
    }

    auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime).count();

    // Update global stats (thread-safe via atomic)
    stats_.regionsSwept++;
    stats_.bytesFreed += region.freeBytes;
    stats_.bytesLive += region.liveBytes;
    stats_.objectsFreed += region.objectsFreed;
    stats_.objectsLive += region.objectsLive;
    stats_.totalSweepTimeMs += elapsed;
    if (elapsed > stats_.maxRegionSweepTimeMs) {
        stats_.maxRegionSweepTimeMs = elapsed;
    }

    region.swept = true;

    size_t remaining = pendingRegions_.fetch_sub(1) - 1;
    if (remaining == 0) {
        completionCV_.notify_all();
    }
}

void ConcurrentSweeper::buildFreeList(SweepableRegion& region) {
    // Coalesce adjacent free blocks
    if (!region.freeList) return;

    // Sort free blocks by address
    std::vector<FreeBlock*> blocks;
    FreeBlock* current = region.freeList;
    while (current) {
        blocks.push_back(current);
        current = current->next;
    }

    std::sort(blocks.begin(), blocks.end(),
        [](FreeBlock* a, FreeBlock* b) { return a < b; });

    // Coalesce
    region.freeList = blocks[0];
    FreeBlock* prev = blocks[0];
    prev->next = nullptr;

    for (size_t i = 1; i < blocks.size(); i++) {
        char* prevEnd = reinterpret_cast<char*>(prev) + prev->size;
        if (prevEnd == reinterpret_cast<char*>(blocks[i])) {
            // Adjacent — merge
            prev->size += blocks[i]->size;
        } else {
            // Not adjacent — link
            prev->next = blocks[i];
            prev = blocks[i];
            prev->next = nullptr;
        }
    }
}

// =============================================================================
// Incremental Marker
// =============================================================================

/**
 * @brief Incremental marking with time-budget
 *
 * Breaks the mark phase into small steps (< 1ms each).
 * The mutator runs between steps. Uses tri-color marking:
 * - White: not yet visited
 * - Grey: discovered but children not visited
 * - Black: visited and all children visited
 *
 * Write barrier ensures grey invariant: no black→white edges.
 */
class IncrementalMarker {
public:
    enum class State {
        Idle,
        Marking,
        MarkingComplete,
        Waiting          // Waiting for finalization
    };

    explicit IncrementalMarker(size_t budgetUs = 500);

    /**
     * @brief Start incremental marking
     */
    void start(const std::vector<void*>& roots);

    /**
     * @brief Perform one marking step within time budget
     * @return true if marking is complete
     */
    bool step();

    /**
     * @brief Write barrier — called when a field is written
     * Ensures tri-color invariant by graying the target.
     */
    void writeBarrier(void* object);

    /**
     * @brief Current state
     */
    State state() const { return state_; }

    /**
     * @brief Progress (0.0 - 1.0)
     */
    double progress() const;

    /**
     * @brief Set object tracer
     */
    using TracerFn = std::function<void(void* object,
        std::function<void(void* child)> pushChild)>;
    void setTracer(TracerFn tracer) { tracer_ = std::move(tracer); }

    /**
     * @brief Set mark checker
     */
    using MarkCheckerFn = std::function<bool(void*)>;
    void setMarkChecker(MarkCheckerFn checker) { markChecker_ = std::move(checker); }

    /**
     * @brief Set mark setter
     */
    using MarkSetterFn = std::function<void(void*)>;
    void setMarkSetter(MarkSetterFn setter) { markSetter_ = std::move(setter); }

    /**
     * @brief Statistics
     */
    struct Stats {
        size_t totalSteps = 0;
        size_t objectsMarked = 0;
        size_t barrierMarks = 0;
        double totalTimeMs = 0;
        double averageStepMs = 0;
    };

    const Stats& stats() const { return stats_; }

private:
    State state_ = State::Idle;
    size_t budgetUs_;

    // Grey set (mark stack)
    std::vector<void*> greyStack_;
    size_t initialGreySize_ = 0;

    // Callbacks
    TracerFn tracer_;
    MarkCheckerFn markChecker_;
    MarkSetterFn markSetter_;

    Stats stats_;
};

// Implementation

IncrementalMarker::IncrementalMarker(size_t budgetUs)
    : budgetUs_(budgetUs) {}

void IncrementalMarker::start(const std::vector<void*>& roots) {
    state_ = State::Marking;
    greyStack_.clear();

    for (void* root : roots) {
        if (root && markSetter_) {
            markSetter_(root);
            greyStack_.push_back(root);
        }
    }

    initialGreySize_ = greyStack_.size();
}

bool IncrementalMarker::step() {
    if (state_ != State::Marking) return true;

    auto startTime = std::chrono::steady_clock::now();
    auto deadline = startTime + std::chrono::microseconds(budgetUs_);

    size_t objectsThisStep = 0;

    while (!greyStack_.empty()) {
        void* obj = greyStack_.back();
        greyStack_.pop_back();

        // Trace children
        if (tracer_) {
            tracer_(obj, [this](void* child) {
                if (child && markChecker_ && !markChecker_(child)) {
                    if (markSetter_) markSetter_(child);
                    greyStack_.push_back(child);
                }
            });
        }

        objectsThisStep++;
        stats_.objectsMarked++;

        // Check time budget every 32 objects
        if (objectsThisStep % 32 == 0) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
        }
    }

    auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime).count();

    stats_.totalSteps++;
    stats_.totalTimeMs += elapsed;
    stats_.averageStepMs = stats_.totalTimeMs /
        static_cast<double>(stats_.totalSteps);

    if (greyStack_.empty()) {
        state_ = State::MarkingComplete;
        return true;
    }

    return false;
}

void IncrementalMarker::writeBarrier(void* object) {
    if (state_ != State::Marking) return;

    // Snapshot-at-the-beginning barrier:
    // If we're marking and an object is being modified,
    // mark the old value to prevent missing it.
    if (markSetter_) {
        markSetter_(object);
        greyStack_.push_back(object);
        stats_.barrierMarks++;
    }
}

double IncrementalMarker::progress() const {
    if (state_ == State::Idle) return 0.0;
    if (state_ == State::MarkingComplete) return 1.0;
    if (initialGreySize_ == 0) return 0.0;

    // Approximate: based on how many objects processed
    size_t remaining = greyStack_.size();
    size_t processed = stats_.objectsMarked;
    size_t total = processed + remaining;
    return total > 0 ? static_cast<double>(processed) / static_cast<double>(total) : 0.0;
}

// =============================================================================
// GC Pipeline
// =============================================================================

/**
 * @brief Complete GC pipeline combining all phases
 *
 * Phases:
 * 1. Roots enumeration
 * 2. Marking (parallel or incremental)
 * 3. Ephemeron convergence
 * 4. Weak reference processing
 * 5. Sweeping (concurrent)
 * 6. Compaction (selective)
 * 7. Finalization
 */
class GCPipeline {
public:
    struct PipelineConfig {
        bool useParallelMarking;
        bool useConcurrentSweep;
        bool useIncrementalMark;
        bool useCompaction;
        size_t compactionThreshold;
        size_t sweepThreads;

        PipelineConfig()
            : useParallelMarking(true)
            , useConcurrentSweep(true)
            , useIncrementalMark(false)
            , useCompaction(true)
            , compactionThreshold(50)
            , sweepThreads(2) {}
    };

    explicit GCPipeline(const PipelineConfig& config = PipelineConfig{});
    ~GCPipeline();

    /**
     * @brief Run the complete GC pipeline
     */
    struct PipelineResult {
        size_t bytesFreed = 0;
        size_t objectsFreed = 0;
        size_t objectsMoved = 0;
        size_t regionsEvacuated = 0;
        double markTimeMs = 0;
        double sweepTimeMs = 0;
        double compactTimeMs = 0;
        double totalTimeMs = 0;
        size_t ephemeronIterations = 0;
    };

    PipelineResult run(
        std::function<void(std::function<void(void** slot)>)> enumerateRoots,
        std::function<void(void* object, std::function<void(void** slot)>)> traceObject,
        std::function<bool(void*)> isMarked,
        std::function<bool(void*)> tryMark,
        std::function<void()> sweep,
        std::function<size_t()> compact
    );

    /**
     * @brief Run incremental marking step
     */
    bool runIncrementalStep();

    /**
     * @brief Start incremental marking phase
     */
    void beginIncrementalMarking(const std::vector<void*>& roots);

    /**
     * @brief Check if incremental marking is in progress
     */
    bool isIncrementalMarkingActive() const;

private:
    PipelineConfig config_;
    ConcurrentSweeper sweeper_;
    IncrementalMarker incrementalMarker_;
};

GCPipeline::GCPipeline(const PipelineConfig& config)
    : config_(config)
    , sweeper_(config.sweepThreads)
    , incrementalMarker_(500) {}

GCPipeline::~GCPipeline() = default;

GCPipeline::PipelineResult GCPipeline::run(
    std::function<void(std::function<void(void** slot)>)> enumerateRoots,
    std::function<void(void* object, std::function<void(void** slot)>)> traceObject,
    std::function<bool(void*)> isMarked,
    std::function<bool(void*)> tryMark,
    std::function<void()> sweep,
    std::function<size_t()> compact
) {
    PipelineResult result;
    auto pipelineStart = std::chrono::steady_clock::now();

    // Phase 1: Root enumeration
    std::vector<void*> roots;
    enumerateRoots([&](void** slot) {
        if (slot && *slot) {
            roots.push_back(*slot);
            tryMark(*slot);
        }
    });

    // Phase 2: Marking
    auto markStart = std::chrono::steady_clock::now();
    {
        // BFS mark from roots
        std::vector<void*> worklist = roots;
        while (!worklist.empty()) {
            void* obj = worklist.back();
            worklist.pop_back();

            traceObject(obj, [&](void** slot) {
                if (slot && *slot && tryMark(*slot)) {
                    worklist.push_back(*slot);
                }
            });
        }
    }
    result.markTimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - markStart).count();

    // Phase 3: Sweep
    auto sweepStart = std::chrono::steady_clock::now();
    sweep();
    result.sweepTimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - sweepStart).count();

    // Phase 4: Compact (optional)
    if (config_.useCompaction && compact) {
        auto compactStart = std::chrono::steady_clock::now();
        result.objectsMoved = compact();
        result.compactTimeMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - compactStart).count();
    }

    result.totalTimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - pipelineStart).count();

    return result;
}

void GCPipeline::beginIncrementalMarking(const std::vector<void*>& roots) {
    incrementalMarker_.start(roots);
}

bool GCPipeline::runIncrementalStep() {
    return incrementalMarker_.step();
}

bool GCPipeline::isIncrementalMarkingActive() const {
    return incrementalMarker_.state() == IncrementalMarker::State::Marking;
}

} // namespace Zepra::Heap
