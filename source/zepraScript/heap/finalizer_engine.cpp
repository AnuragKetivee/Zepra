// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file finalizer_engine.cpp
 * @brief FinalizationRegistry and destructor scheduling
 *
 * Implements ES2021 FinalizationRegistry semantics:
 * - Registering targets with a cleanup callback
 * - Unregistering via unregister tokens
 * - Scheduling cleanup callbacks after GC
 * - Non-deterministic cleanup (callbacks run "eventually")
 *
 * Also handles C++ destructor invocation for host objects:
 * - NativeDisposer: invoked synchronously during GC sweep
 * - PostGCDisposer: deferred to after GC completes
 *
 * Callback scheduling:
 * - Callbacks are NOT run during GC (that would mutate the heap)
 * - Instead, they're queued and run via microtask or idle callback
 * - The engine drains the finalization queue periodically
 *
 * Order of operations:
 * 1. GC marking completes
 * 2. Weak processing clears dead WeakCells
 * 3. FinalizerEngine collects dead registrations → cleanup queue
 * 4. GC sweep runs NativeDisposers
 * 5. GC completes
 * 6. Microtask checkpoint drains cleanup queue
 */

#include <atomic>
#include <mutex>
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
#include <unordered_map>

namespace Zepra::Heap {

// =============================================================================
// Registration Entry
// =============================================================================

struct FinalizationEntry {
    uint64_t entryId;

    void* target;           // Weakly-held target object
    void* heldValue;        // Value passed to cleanup callback
    void* unregisterToken;  // Token for unregistration (may be null)

    uint32_t registryId;    // Which FinalizationRegistry owns this
    bool cleared;           // Target has been collected
    bool dispatched;        // Cleanup callback has been queued
};

// =============================================================================
// Finalization Registry
// =============================================================================

/**
 * @brief Single FinalizationRegistry instance
 *
 * Each JS FinalizationRegistry object maps to one of these.
 * It holds a cleanup callback and a list of registrations.
 */
class FinalizationRegistry {
public:
    using CleanupCallback = std::function<void(void* heldValue)>;

    explicit FinalizationRegistry(uint32_t id, CleanupCallback callback)
        : id_(id), callback_(std::move(callback)) {}

    uint32_t id() const { return id_; }

    /**
     * @brief Register a target for cleanup
     */
    uint64_t registerTarget(void* target, void* heldValue,
                            void* unregisterToken) {
        uint64_t entryId = nextEntryId_++;

        FinalizationEntry entry;
        entry.entryId = entryId;
        entry.target = target;
        entry.heldValue = heldValue;
        entry.unregisterToken = unregisterToken;
        entry.registryId = id_;
        entry.cleared = false;
        entry.dispatched = false;

        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back(entry);

        if (unregisterToken) {
            tokenMap_[reinterpret_cast<uintptr_t>(unregisterToken)]
                .push_back(entryId);
        }

        return entryId;
    }

    /**
     * @brief Unregister entries with given token
     * @return Number of entries removed
     */
    size_t unregister(void* token) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto key = reinterpret_cast<uintptr_t>(token);
        auto it = tokenMap_.find(key);
        if (it == tokenMap_.end()) return 0;

        auto& ids = it->second;
        size_t removed = 0;

        for (uint64_t id : ids) {
            auto entryIt = std::find_if(entries_.begin(), entries_.end(),
                [id](const FinalizationEntry& e) {
                    return e.entryId == id;
                });
            if (entryIt != entries_.end()) {
                entries_.erase(entryIt);
                removed++;
            }
        }

        tokenMap_.erase(it);
        return removed;
    }

    /**
     * @brief Mark entries whose target is dead
     * @return Number of newly cleared entries
     */
    size_t processDeadTargets(std::function<bool(void*)> isMarked) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t cleared = 0;

        for (auto& entry : entries_) {
            if (entry.cleared || entry.dispatched) continue;
            if (!entry.target) continue;

            if (!isMarked(entry.target)) {
                entry.cleared = true;
                entry.target = nullptr;
                cleared++;
            }
        }

        return cleared;
    }

    /**
     * @brief Collect cleared entries into cleanup queue
     * @return Number of entries collected
     */
    size_t collectCleared(std::vector<void*>& heldValues) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t collected = 0;

        auto it = entries_.begin();
        while (it != entries_.end()) {
            if (it->cleared && !it->dispatched) {
                heldValues.push_back(it->heldValue);
                it->dispatched = true;
                collected++;
            }
            ++it;
        }

        return collected;
    }

    /**
     * @brief Run cleanup callback for given held values
     */
    void runCleanup(const std::vector<void*>& heldValues) {
        for (void* heldValue : heldValues) {
            if (callback_) {
                callback_(heldValue);
            }
        }
    }

    /**
     * @brief Remove dispatched entries (housekeeping)
     */
    void purgeDispatched() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [](const FinalizationEntry& e) { return e.dispatched; }),
            entries_.end());
    }

    size_t entryCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

    size_t pendingCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t count = 0;
        for (const auto& e : entries_) {
            if (e.cleared && !e.dispatched) count++;
        }
        return count;
    }

private:
    uint32_t id_;
    CleanupCallback callback_;
    std::vector<FinalizationEntry> entries_;
    std::unordered_map<uintptr_t, std::vector<uint64_t>> tokenMap_;
    mutable std::mutex mutex_;
    uint64_t nextEntryId_ = 1;
};

// =============================================================================
// Native Disposer
// =============================================================================

/**
 * @brief C++ destructor for host objects
 *
 * When a host-created JS object is collected, its native
 * destructor must be called to free native resources.
 */
struct NativeDisposer {
    using DisposeFn = void (*)(void* nativePtr);

    void* nativePtr;
    DisposeFn disposeFn;
    size_t externalBytes;   // External memory to report freed
    const char* typeName;   // For diagnostics
};

// =============================================================================
// Finalizer Engine
// =============================================================================

class FinalizerEngine {
public:
    struct Stats {
        size_t registriesActive;
        size_t totalRegistrations;
        size_t totalFinalized;
        size_t cleanupCallbacksRun;
        size_t nativeDisposersRun;
        size_t externalBytesFreed;
        double lastProcessingMs;
    };

    FinalizerEngine() = default;
    ~FinalizerEngine() = default;

    FinalizerEngine(const FinalizerEngine&) = delete;
    FinalizerEngine& operator=(const FinalizerEngine&) = delete;

    // -------------------------------------------------------------------------
    // Registry management
    // -------------------------------------------------------------------------

    /**
     * @brief Create a new FinalizationRegistry
     */
    uint32_t createRegistry(FinalizationRegistry::CleanupCallback callback) {
        uint32_t id = nextRegistryId_++;
        std::lock_guard<std::mutex> lock(mutex_);
        registries_[id] = std::make_unique<FinalizationRegistry>(
            id, std::move(callback));
        return id;
    }

    /**
     * @brief Destroy a registry (e.g. when the JS object is collected)
     */
    void destroyRegistry(uint32_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        registries_.erase(id);
    }

    FinalizationRegistry* getRegistry(uint32_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = registries_.find(id);
        return it != registries_.end() ? it->second.get() : nullptr;
    }

    // -------------------------------------------------------------------------
    // Native disposer registration
    // -------------------------------------------------------------------------

    /**
     * @brief Register a native disposer for an object
     * @param objectAddr Address of the JS object
     */
    void registerDisposer(uintptr_t objectAddr, NativeDisposer disposer) {
        std::lock_guard<std::mutex> lock(mutex_);
        disposers_[objectAddr] = disposer;
    }

    void unregisterDisposer(uintptr_t objectAddr) {
        std::lock_guard<std::mutex> lock(mutex_);
        disposers_.erase(objectAddr);
    }

    // -------------------------------------------------------------------------
    // GC processing
    // -------------------------------------------------------------------------

    /**
     * @brief Process dead targets in all registries
     * Called after marking, before sweep.
     */
    size_t processDeadTargets(std::function<bool(void*)> isMarked) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t cleared = 0;

        for (auto& [id, registry] : registries_) {
            cleared += registry->processDeadTargets(isMarked);
        }

        return cleared;
    }

    /**
     * @brief Invoke native disposers for dead objects
     * Called during sweep when a dead object with a disposer is found.
     */
    size_t disposeNative(uintptr_t deadObjectAddr) {
        NativeDisposer disposer;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = disposers_.find(deadObjectAddr);
            if (it == disposers_.end()) return 0;
            disposer = it->second;
            disposers_.erase(it);
        }

        if (disposer.disposeFn) {
            disposer.disposeFn(disposer.nativePtr);
        }

        totalNativeDisposers_++;
        totalExternalBytesFreed_ += disposer.externalBytes;
        return disposer.externalBytes;
    }

    /**
     * @brief Collect pending cleanup callbacks
     *
     * Collects held values from all registries with dead targets.
     * The caller should invoke these callbacks outside the GC pause.
     */
    struct CleanupBatch {
        uint32_t registryId;
        std::vector<void*> heldValues;
    };

    std::vector<CleanupBatch> collectCleanupBatches() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<CleanupBatch> batches;

        for (auto& [id, registry] : registries_) {
            CleanupBatch batch;
            batch.registryId = id;
            registry->collectCleared(batch.heldValues);
            if (!batch.heldValues.empty()) {
                batches.push_back(std::move(batch));
            }
        }

        return batches;
    }

    /**
     * @brief Run cleanup callbacks from collected batches
     * Called outside GC pause (e.g. during microtask checkpoint)
     */
    size_t runCleanupBatches(const std::vector<CleanupBatch>& batches) {
        size_t total = 0;

        for (const auto& batch : batches) {
            auto* registry = getRegistry(batch.registryId);
            if (registry) {
                registry->runCleanup(batch.heldValues);
                total += batch.heldValues.size();
            }
        }

        totalCleanupCallbacks_ += total;
        return total;
    }

    /**
     * @brief Housekeeping: remove dispatched entries
     */
    void purgeAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, registry] : registries_) {
            registry->purgeDispatched();
        }
    }

    Stats computeStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Stats stats{};
        stats.registriesActive = registries_.size();

        for (const auto& [id, registry] : registries_) {
            stats.totalRegistrations += registry->entryCount();
        }

        stats.totalFinalized = totalCleanupCallbacks_ + totalNativeDisposers_;
        stats.cleanupCallbacksRun = totalCleanupCallbacks_;
        stats.nativeDisposersRun = totalNativeDisposers_;
        stats.externalBytesFreed = totalExternalBytesFreed_;

        return stats;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, std::unique_ptr<FinalizationRegistry>> registries_;
    std::unordered_map<uintptr_t, NativeDisposer> disposers_;

    uint32_t nextRegistryId_ = 1;
    size_t totalCleanupCallbacks_ = 0;
    size_t totalNativeDisposers_ = 0;
    size_t totalExternalBytesFreed_ = 0;
};

} // namespace Zepra::Heap
