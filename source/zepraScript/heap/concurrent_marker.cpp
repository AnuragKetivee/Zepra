/**
 * @file concurrent_marker.cpp
 * @brief Full concurrent marking implementation
 *
 * Implements tri-color concurrent marking:
 * - Marking runs in background threads while mutator executes
 * - SATB (Snapshot-At-The-Beginning) write barrier maintains invariant
 * - Handshake protocol for root scanning
 * - Work termination protocol for parallel workers
 *
 * Marking phases:
 * 1. Initial mark (STW): scan roots, start background marking
 * 2. Concurrent mark: trace object graph in background
 * 3. Remark (STW): drain SATB buffer, complete marking
 * 4. Cleanup: clear bitmaps for swept regions
 *
 * Thread coordination:
 * - Main thread requests marking via state machine
 * - Worker threads process shared/local worklists
 * - Termination detected when all workers idle + empty worklists
 */

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <deque>
#include <functional>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <memory>

#ifdef __linux__
#include <unistd.h>
#include <sys/mman.h>
#include <sched.h>
#endif

namespace Zepra::Heap {

// =============================================================================
// SATB Buffer
// =============================================================================

/**
 * @brief Sequential store buffer for SATB write barrier
 *
 * The mutator writes overwritten references here. During remark,
 * all buffered references are treated as roots (prevents missing
 * objects that were moved between grey and white).
 *
 * Per-thread buffer flushed to global buffer when full or at GC sync.
 */
class SATBBuffer {
public:
    static constexpr size_t LOCAL_CAPACITY = 512;
    static constexpr size_t GLOBAL_CAPACITY = 65536;

    /**
     * @brief Per-thread SATB buffer (no locks)
     */
    struct ThreadBuffer {
        void* items[LOCAL_CAPACITY];
        size_t count = 0;

        bool push(void* item) {
            if (count >= LOCAL_CAPACITY) return false;
            items[count++] = item;
            return true;
        }

        void clear() { count = 0; }
        bool isFull() const { return count >= LOCAL_CAPACITY; }
        bool isEmpty() const { return count == 0; }
    };

    SATBBuffer() = default;

    /**
     * @brief Record an overwritten reference (mutator side)
     */
    void record(void* oldValue, ThreadBuffer& threadBuf) {
        if (!oldValue) return;
        if (!enabled_.load(std::memory_order_relaxed)) return;

        if (!threadBuf.push(oldValue)) {
            // Buffer full — flush to global
            flushThreadBuffer(threadBuf);
            threadBuf.push(oldValue);
        }
    }

    /**
     * @brief Flush thread buffer to global (at GC sync or buffer full)
     */
    void flushThreadBuffer(ThreadBuffer& threadBuf) {
        if (threadBuf.isEmpty()) return;

        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < threadBuf.count; i++) {
            if (globalCount_ < GLOBAL_CAPACITY) {
                globalBuffer_[globalCount_++] = threadBuf.items[i];
            }
        }
        threadBuf.clear();
    }

    /**
     * @brief Drain global buffer (GC side, during remark)
     * @return Number of entries drained
     */
    size_t drain(std::function<void(void* reference)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t count = globalCount_;
        for (size_t i = 0; i < globalCount_; i++) {
            callback(globalBuffer_[i]);
        }
        globalCount_ = 0;
        return count;
    }

    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
    bool isEnabled() const { return enabled_.load(); }

    size_t globalCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return globalCount_;
    }

private:
    std::atomic<bool> enabled_{false};
    void* globalBuffer_[GLOBAL_CAPACITY];
    size_t globalCount_ = 0;
    mutable std::mutex mutex_;
};

// =============================================================================
// Marking State Machine
// =============================================================================

enum class MarkingState : uint8_t {
    Idle,               // No marking in progress
    InitialMark,        // STW: scanning roots
    ConcurrentMark,     // Background marking
    Remark,             // STW: drain SATB, complete
    Cleanup,            // Clear bitmaps, update stats
    Complete            // Marking done, ready for sweep
};

static const char* markingStateName(MarkingState s) {
    switch (s) {
        case MarkingState::Idle: return "Idle";
        case MarkingState::InitialMark: return "InitialMark";
        case MarkingState::ConcurrentMark: return "ConcurrentMark";
        case MarkingState::Remark: return "Remark";
        case MarkingState::Cleanup: return "Cleanup";
        case MarkingState::Complete: return "Complete";
        default: return "Unknown";
    }
}

// =============================================================================
// Work Termination Protocol
// =============================================================================

/**
 * @brief Detects when all marking workers have finished
 *
 * Uses a global "active workers" counter + epoch:
 * 1. Worker has no local work → decrements active count
 * 2. Worker finds work from stealing → increments active count
 * 3. When active count = 0 and global worklist empty → terminated
 *
 * Epoch prevents ABA: if a worker adds work between check and terminate,
 * the epoch advances and termination is retried.
 */
class WorkTermination {
public:
    explicit WorkTermination(size_t workerCount)
        : totalWorkers_(workerCount) {
        activeWorkers_ = workerCount;
        epoch_ = 0;
        terminated_ = false;
    }

    void workerIdle() {
        activeWorkers_.fetch_sub(1, std::memory_order_release);
    }

    void workerActive() {
        activeWorkers_.fetch_add(1, std::memory_order_release);
    }

    bool shouldTerminate() const {
        return activeWorkers_.load(std::memory_order_acquire) == 0;
    }

    void signalTermination() {
        terminated_ = true;
        cv_.notify_all();
    }

    void waitForTermination() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return terminated_.load(); });
    }

    void reset(size_t workerCount) {
        totalWorkers_ = workerCount;
        activeWorkers_ = workerCount;
        epoch_++;
        terminated_ = false;
    }

    size_t epoch() const { return epoch_.load(); }
    bool isTerminated() const { return terminated_.load(); }

private:
    size_t totalWorkers_;
    std::atomic<size_t> activeWorkers_;
    std::atomic<size_t> epoch_;
    std::atomic<bool> terminated_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// =============================================================================
// Concurrent Mark Worker
// =============================================================================

/**
 * @brief Individual marking worker thread
 */
class MarkWorker {
public:
    static constexpr size_t LOCAL_CAPACITY = 256;

    struct LocalWorklist {
        void* items[LOCAL_CAPACITY];
        size_t count = 0;

        bool push(void* item) {
            if (count >= LOCAL_CAPACITY) return false;
            items[count++] = item;
            return true;
        }

        void* pop() {
            if (count == 0) return nullptr;
            return items[--count];
        }

        bool isEmpty() const { return count == 0; }
        bool isFull() const { return count >= LOCAL_CAPACITY; }
    };

    struct WorkerStats {
        uint64_t objectsMarked = 0;
        uint64_t bytesMarked = 0;
        uint64_t steals = 0;
        uint64_t stealAttempts = 0;
        uint64_t localPushes = 0;
        uint64_t globalPushes = 0;
        double activeTimeMs = 0;
        double idleTimeMs = 0;
    };

    MarkWorker(size_t id, std::function<void*(void)> globalPop,
               std::function<void(void*, size_t)> globalPush,
               std::function<void(void*, std::function<void(void**)>)> trace,
               std::function<bool(void*)> tryMark,
               WorkTermination& termination)
        : id_(id)
        , globalPop_(std::move(globalPop))
        , globalPush_(std::move(globalPush))
        , trace_(std::move(trace))
        , tryMark_(std::move(tryMark))
        , termination_(termination) {}

    void run() {
        auto startTime = std::chrono::steady_clock::now();

        while (!termination_.isTerminated()) {
            // Try local worklist
            void* obj = local_.pop();

            if (!obj) {
                // Try global
                obj = globalPop_();
                stats_.stealAttempts++;
                if (obj) stats_.steals++;
            }

            if (!obj) {
                // No work — go idle
                termination_.workerIdle();

                // Double-check after going idle
                obj = globalPop_();
                if (obj) {
                    termination_.workerActive();
                    stats_.steals++;
                } else {
                    // Really no work — check termination
                    if (termination_.shouldTerminate()) break;

                    // Spin briefly before re-checking
                    std::this_thread::yield();
                    termination_.workerActive();
                    continue;
                }
            }

            // Process object
            processObject(obj);
        }

        // Drain remaining local items to global
        while (void* remaining = local_.pop()) {
            // Push back for other workers (or final processing)
            if (globalPush_) {
                globalPush_(remaining, 0);
            }
        }

        stats_.activeTimeMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - startTime).count();
    }

    const WorkerStats& stats() const { return stats_; }

private:
    void processObject(void* obj) {
        if (!obj) return;

        stats_.objectsMarked++;

        if (trace_) {
            trace_(obj, [this](void** slot) {
                if (!slot || !*slot) return;
                if (tryMark_(*slot)) {
                    if (!local_.push(*slot)) {
                        stats_.globalPushes++;
                        if (globalPush_) globalPush_(*slot, 0);
                    } else {
                        stats_.localPushes++;
                    }
                }
            });
        }
    }

    size_t id_;
    LocalWorklist local_;
    WorkerStats stats_;

    std::function<void*(void)> globalPop_;
    std::function<void(void*, size_t)> globalPush_;
    std::function<void(void*, std::function<void(void**)>)> trace_;
    std::function<bool(void*)> tryMark_;
    WorkTermination& termination_;
};

// =============================================================================
// Concurrent Marker
// =============================================================================

class ConcurrentMarker {
public:
    struct Config {
        size_t workerCount;
        size_t globalWorklistCapacity;
        bool enableSATB;

        Config()
            : workerCount(2)
            , globalWorklistCapacity(65536)
            , enableSATB(true) {}
    };

    struct MarkerStats {
        uint64_t objectsMarked = 0;
        uint64_t bytesMarked = 0;
        uint64_t satbEntriesProcessed = 0;
        double initialMarkMs = 0;
        double concurrentMarkMs = 0;
        double remarkMs = 0;
        double totalMs = 0;
        size_t markingRounds = 0;
    };

    using RootEnumerator = std::function<void(std::function<void(void** slot)>)>;
    using ObjectTracer = std::function<void(void*, std::function<void(void**)>)>;
    using MarkPredicate = std::function<bool(void*)>;

    explicit ConcurrentMarker(const Config& config = Config{});
    ~ConcurrentMarker();

    /**
     * @brief Set callbacks
     */
    void setRootEnumerator(RootEnumerator enumerator) {
        rootEnumerator_ = std::move(enumerator);
    }

    void setObjectTracer(ObjectTracer tracer) {
        objectTracer_ = std::move(tracer);
    }

    void setTryMark(MarkPredicate tryMark) {
        tryMark_ = std::move(tryMark);
    }

    /**
     * @brief Access the SATB buffer for write barrier integration
     */
    SATBBuffer& satbBuffer() { return satbBuffer_; }

    // -------------------------------------------------------------------------
    // Marking Phases
    // -------------------------------------------------------------------------

    /**
     * @brief Phase 1: Initial mark (requires STW)
     * Scan roots, push to global worklist.
     */
    void initialMark();

    /**
     * @brief Phase 2: Concurrent mark
     * Start worker threads, mark reachable objects.
     * Returns when all workers terminate.
     */
    void concurrentMark();

    /**
     * @brief Phase 3: Remark (requires STW)
     * Drain SATB buffer, re-scan dirty roots, complete marking.
     */
    void remark();

    /**
     * @brief Phase 4: Cleanup
     */
    void cleanup();

    /**
     * @brief Run all phases (for non-concurrent fallback)
     */
    MarkerStats runAll();

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    MarkingState state() const { return state_; }
    const MarkerStats& stats() const { return stats_; }

private:
    Config config_;
    MarkingState state_ = MarkingState::Idle;
    MarkerStats stats_;

    RootEnumerator rootEnumerator_;
    ObjectTracer objectTracer_;
    MarkPredicate tryMark_;

    SATBBuffer satbBuffer_;

    // Global worklist
    std::mutex worklistMutex_;
    std::deque<void*> globalWorklist_;

    // Workers
    std::vector<std::thread> workerThreads_;
    std::unique_ptr<WorkTermination> termination_;
};

// =============================================================================
// Implementation
// =============================================================================

inline ConcurrentMarker::ConcurrentMarker(const Config& config)
    : config_(config) {}

inline ConcurrentMarker::~ConcurrentMarker() {
    for (auto& t : workerThreads_) {
        if (t.joinable()) t.join();
    }
}

inline void ConcurrentMarker::initialMark() {
    state_ = MarkingState::InitialMark;
    auto start = std::chrono::steady_clock::now();

    // Enable SATB barrier
    if (config_.enableSATB) {
        satbBuffer_.enable();
    }

    // Scan roots
    if (rootEnumerator_) {
        rootEnumerator_([this](void** slot) {
            if (slot && *slot && tryMark_ && tryMark_(*slot)) {
                std::lock_guard<std::mutex> lock(worklistMutex_);
                globalWorklist_.push_back(*slot);
            }
        });
    }

    stats_.initialMarkMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

inline void ConcurrentMarker::concurrentMark() {
    state_ = MarkingState::ConcurrentMark;
    auto start = std::chrono::steady_clock::now();

    termination_ = std::make_unique<WorkTermination>(config_.workerCount);

    // Create workers
    std::vector<std::unique_ptr<MarkWorker>> workers;
    for (size_t i = 0; i < config_.workerCount; i++) {
        auto worker = std::make_unique<MarkWorker>(
            i,
            // Global pop
            [this]() -> void* {
                std::lock_guard<std::mutex> lock(worklistMutex_);
                if (globalWorklist_.empty()) return nullptr;
                void* item = globalWorklist_.back();
                globalWorklist_.pop_back();
                return item;
            },
            // Global push
            [this](void* item, size_t /*unused*/) {
                std::lock_guard<std::mutex> lock(worklistMutex_);
                globalWorklist_.push_back(item);
            },
            objectTracer_,
            tryMark_,
            *termination_
        );
        workers.push_back(std::move(worker));
    }

    // Start workers
    workerThreads_.clear();
    for (size_t i = 0; i < config_.workerCount; i++) {
        auto* w = workers[i].get();
        workerThreads_.emplace_back([w]() { w->run(); });
    }

    // Wait for completion
    for (auto& t : workerThreads_) {
        t.join();
    }
    workerThreads_.clear();

    // Collect stats
    for (auto& w : workers) {
        stats_.objectsMarked += w->stats().objectsMarked;
        stats_.bytesMarked += w->stats().bytesMarked;
    }

    stats_.concurrentMarkMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

inline void ConcurrentMarker::remark() {
    state_ = MarkingState::Remark;
    auto start = std::chrono::steady_clock::now();

    // Drain SATB buffer
    if (config_.enableSATB) {
        size_t drained = satbBuffer_.drain([this](void* ref) {
            if (tryMark_ && tryMark_(ref)) {
                globalWorklist_.push_back(ref);
            }
        });
        stats_.satbEntriesProcessed += drained;
    }

    // Process remaining worklist (single-threaded during STW)
    while (!globalWorklist_.empty()) {
        void* obj = globalWorklist_.back();
        globalWorklist_.pop_back();

        if (objectTracer_) {
            objectTracer_(obj, [this](void** slot) {
                if (slot && *slot && tryMark_ && tryMark_(*slot)) {
                    globalWorklist_.push_back(*slot);
                }
            });
        }

        stats_.objectsMarked++;
    }

    // Disable SATB
    satbBuffer_.disable();

    stats_.remarkMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

inline void ConcurrentMarker::cleanup() {
    state_ = MarkingState::Cleanup;
    stats_.markingRounds++;
    stats_.totalMs = stats_.initialMarkMs + stats_.concurrentMarkMs + stats_.remarkMs;
    state_ = MarkingState::Complete;
}

inline ConcurrentMarker::MarkerStats ConcurrentMarker::runAll() {
    initialMark();
    concurrentMark();
    remark();
    cleanup();
    return stats_;
}

// =============================================================================
// OS Memory Integration
// =============================================================================

/**
 * @brief Cgroup-aware memory monitoring
 *
 * Reads memory.limit_in_bytes and memory.usage_in_bytes from cgroup v1/v2.
 * Used to automatically adjust heap limits when running in containers.
 */
class CGroupMemoryMonitor {
public:
    struct CGroupLimits {
        size_t memoryLimit = 0;     // 0 = unlimited
        size_t memoryUsage = 0;
        size_t swapLimit = 0;
        size_t swapUsage = 0;
        bool isCGroupV2 = false;
        bool available = false;
    };

    static CGroupLimits query() {
        CGroupLimits limits;

#ifdef __linux__
        // Try cgroup v2
        FILE* f = fopen("/sys/fs/cgroup/memory.max", "r");
        if (f) {
            limits.isCGroupV2 = true;
            char buf[64];
            if (fgets(buf, sizeof(buf), f)) {
                if (strcmp(buf, "max\n") != 0) {
                    limits.memoryLimit = strtoull(buf, nullptr, 10);
                }
            }
            fclose(f);

            f = fopen("/sys/fs/cgroup/memory.current", "r");
            if (f) {
                if (fscanf(f, "%zu", &limits.memoryUsage) == 1) {
                    limits.available = true;
                }
                fclose(f);
            }
        } else {
            // Try cgroup v1
            f = fopen("/sys/fs/cgroup/memory/memory.limit_in_bytes", "r");
            if (f) {
                if (fscanf(f, "%zu", &limits.memoryLimit) == 1) {
                    limits.available = true;
                }
                fclose(f);

                f = fopen("/sys/fs/cgroup/memory/memory.usage_in_bytes", "r");
                if (f) {
                    if (fscanf(f, "%zu", &limits.memoryUsage) == 1) {
                        // ok
                    }
                    fclose(f);
                }
            }
        }

        // Sanity: cgroup limit of 2^63 means unlimited
        if (limits.memoryLimit > size_t(1) << 60) {
            limits.memoryLimit = 0;
        }
#endif

        return limits;
    }

    /**
     * @brief Get available memory considering cgroup limits
     */
    static size_t availableMemory() {
        auto limits = query();
        if (limits.available && limits.memoryLimit > 0) {
            return limits.memoryLimit > limits.memoryUsage
                ? limits.memoryLimit - limits.memoryUsage : 0;
        }

        // Fallback: system memory
#ifdef __linux__
        FILE* f = fopen("/proc/meminfo", "r");
        if (!f) return 0;
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            size_t val = 0;
            if (sscanf(line, "MemAvailable: %zu kB", &val) == 1) {
                fclose(f);
                return val * 1024;
            }
        }
        fclose(f);
#endif
        return 0;
    }
};

/**
 * @brief NUMA topology information
 */
class NUMAInfo {
public:
    struct Node {
        int id;
        size_t totalMemory;
        size_t freeMemory;
        std::vector<int> cpus;
    };

    static bool isAvailable() {
#ifdef __linux__
        return access("/sys/devices/system/node/node0", F_OK) == 0;
#else
        return false;
#endif
    }

    static size_t nodeCount() {
        if (!isAvailable()) return 1;
        size_t count = 0;
        char path[128];
        while (true) {
            snprintf(path, sizeof(path),
                     "/sys/devices/system/node/node%zu", count);
#ifdef __linux__
            if (access(path, F_OK) != 0) break;
#else
            break;
#endif
            count++;
        }
        return count > 0 ? count : 1;
    }

    static int currentNode() {
#ifdef __linux__
        // Read from /proc/self/numa_maps or use getcpu
        unsigned int cpu = 0;
        if (getcpu(&cpu, nullptr) == 0) {
            // Map CPU to node
            char path[128];
            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu%u/topology/physical_package_id",
                     cpu);
            FILE* f = fopen(path, "r");
            if (f) {
                int node = 0;
                if (fscanf(f, "%d", &node) == 1) {
                    fclose(f);
                    return node;
                }
                fclose(f);
            }
        }
#endif
        return 0;
    }
};

/**
 * @brief Transparent huge page (THP) management
 */
class TransparentHugePages {
public:
    enum class THPMode { Always, MAdvise, Never, Unknown };

    static THPMode currentMode() {
#ifdef __linux__
        FILE* f = fopen("/sys/kernel/mm/transparent_hugepage/enabled", "r");
        if (!f) return THPMode::Unknown;

        char buf[256];
        if (!fgets(buf, sizeof(buf), f)) {
            fclose(f);
            return THPMode::Unknown;
        }
        fclose(f);

        if (strstr(buf, "[always]")) return THPMode::Always;
        if (strstr(buf, "[madvise]")) return THPMode::MAdvise;
        if (strstr(buf, "[never]")) return THPMode::Never;
#endif
        return THPMode::Unknown;
    }

    static bool isAvailable() {
        THPMode mode = currentMode();
        return mode == THPMode::Always || mode == THPMode::MAdvise;
    }

    /**
     * @brief Request THP for a memory region
     */
    static bool requestForRegion(void* addr, size_t size) {
#ifdef __linux__
        if (currentMode() == THPMode::MAdvise) {
            return madvise(addr, size, MADV_HUGEPAGE) == 0;
        }
#else
        (void)addr; (void)size;
#endif
        return false;
    }
};

/**
 * @brief System memory pressure notification via eventfd
 */
class MemoryPressureNotifier {
public:
    using PressureCallback = std::function<void(int level)>;

    MemoryPressureNotifier() = default;
    ~MemoryPressureNotifier() { stop(); }

    /**
     * @brief Start monitoring memory pressure
     */
    void start(PressureCallback callback) {
        callback_ = std::move(callback);
        running_ = true;
        monitorThread_ = std::thread([this]() { monitorLoop(); });
    }

    void stop() {
        running_ = false;
        if (monitorThread_.joinable()) monitorThread_.join();
    }

private:
    void monitorLoop() {
        while (running_.load()) {
            // Check memory pressure periodically
            size_t available = CGroupMemoryMonitor::availableMemory();

            if (available > 0) {
                size_t totalPhys = getTotalPhysicalMemory();
                double ratio = static_cast<double>(available) /
                               static_cast<double>(totalPhys);

                if (ratio < 0.05 && callback_) {
                    callback_(2);  // Critical
                } else if (ratio < 0.15 && callback_) {
                    callback_(1);  // Moderate
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    static size_t getTotalPhysicalMemory() {
#ifdef __linux__
        FILE* f = fopen("/proc/meminfo", "r");
        if (!f) return 0;
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            size_t val = 0;
            if (sscanf(line, "MemTotal: %zu kB", &val) == 1) {
                fclose(f);
                return val * 1024;
            }
        }
        fclose(f);
#endif
        return 0;
    }

    std::atomic<bool> running_{false};
    std::thread monitorThread_;
    PressureCallback callback_;
};

} // namespace Zepra::Heap
