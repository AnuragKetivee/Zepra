// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file ephemeron_table.cpp
 * @brief Ephemeron (WeakMap/WeakSet) support for GC
 *
 * An ephemeron is a key-value pair where:
 * - The key is held weakly
 * - The value is held strongly IF the key is reachable
 * - If the key is unreachable, both key and value are collectible
 *
 * This is exactly the semantics of WeakMap and WeakSet in JS.
 *
 * The challenge: marking ephemerons requires an iterative fixpoint.
 * Normal marking can't decide ephemeron liveness in one pass because:
 * - Value should be traced only if key is reachable
 * - But key might become reachable via another ephemeron's value
 * - This creates circular dependencies that require multiple passes
 *
 * Algorithm (based on Barros & Ierusalimschy, "Eliminating Cycles"):
 * 1. During marking, ephemerons whose key is already marked:
 *    trace the value immediately
 * 2. Ephemerons whose key is NOT yet marked:
 *    add to the "discovered" list
 * 3. After regular marking completes, iterate the discovered list:
 *    For each ephemeron, if key is now marked → trace value, re-scan
 *    Repeat until no new objects are marked (fixpoint reached)
 * 4. Remaining ephemerons have dead keys → clear them
 *
 * This file implements the ephemeron table, fixpoint iteration,
 * and integration with the main marking loop.
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
#include <unordered_set>

namespace Zepra::Heap {

// =============================================================================
// Ephemeron Entry
// =============================================================================

struct EphemeronEntry {
    void* key;              // Weakly-held key
    void* value;            // Strongly-held if key is alive
    uint32_t bucketIndex;   // Position in hash table
    bool keyMarked;         // Set during fixpoint iteration
    bool valueTraced;       // Value has been traced
    bool dead;              // Key is unreachable → entry is dead
};

// =============================================================================
// Ephemeron Hash Table
// =============================================================================

/**
 * @brief Hash table storing ephemeron entries
 *
 * Open-addressing with linear probing. Tombstone-based deletion.
 * Load factor kept below 0.75 via automatic resizing.
 */
class EphemeronHashTable {
public:
    static constexpr double MAX_LOAD_FACTOR = 0.75;
    static constexpr size_t INITIAL_CAPACITY = 64;

    EphemeronHashTable() {
        resize(INITIAL_CAPACITY);
    }

    /**
     * @brief Insert or update a key-value pair
     */
    void set(void* key, void* value) {
        if (!key) return;

        // Resize if needed
        if (static_cast<double>(size_ + 1) >
            static_cast<double>(capacity_) * MAX_LOAD_FACTOR) {
            resize(capacity_ * 2);
        }

        size_t idx = probe(key);
        auto& slot = table_[idx];

        if (slot.state == SlotState::Empty ||
            slot.state == SlotState::Deleted) {
            slot.key = key;
            slot.value = value;
            slot.state = SlotState::Occupied;
            size_++;
        } else {
            // Update existing
            slot.value = value;
        }
    }

    /**
     * @brief Get value for key (returns null if not found or key is dead)
     */
    void* get(void* key) const {
        if (!key) return nullptr;
        size_t idx = findSlot(key);
        if (idx == SIZE_MAX) return nullptr;
        return table_[idx].value;
    }

    /**
     * @brief Remove entry by key
     */
    bool remove(void* key) {
        if (!key) return false;
        size_t idx = findSlot(key);
        if (idx == SIZE_MAX) return false;

        table_[idx].state = SlotState::Deleted;
        table_[idx].key = nullptr;
        table_[idx].value = nullptr;
        size_--;
        deleted_++;

        // Compact if too many tombstones
        if (deleted_ > capacity_ / 4) {
            rehash();
        }

        return true;
    }

    /**
     * @brief Check if key exists
     */
    bool has(void* key) const {
        if (!key) return false;
        return findSlot(key) != SIZE_MAX;
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }

    /**
     * @brief Iterate all live entries
     */
    void forEach(std::function<void(void* key, void* value)> visitor) const {
        for (size_t i = 0; i < capacity_; i++) {
            if (table_[i].state == SlotState::Occupied) {
                visitor(table_[i].key, table_[i].value);
            }
        }
    }

    /**
     * @brief Clear all entries
     */
    void clear() {
        for (size_t i = 0; i < capacity_; i++) {
            table_[i] = {};
        }
        size_ = 0;
        deleted_ = 0;
    }

private:
    enum class SlotState : uint8_t { Empty, Occupied, Deleted };

    struct Slot {
        void* key = nullptr;
        void* value = nullptr;
        SlotState state = SlotState::Empty;
    };

    size_t hash(void* key) const {
        auto h = reinterpret_cast<uintptr_t>(key);
        // Fibonacci hash
        h ^= h >> 33;
        h *= 0xFF51AFD7ED558CCDULL;
        h ^= h >> 33;
        h *= 0xC4CEB9FE1A85EC53ULL;
        h ^= h >> 33;
        return static_cast<size_t>(h);
    }

    size_t probe(void* key) const {
        size_t mask = capacity_ - 1;
        size_t idx = hash(key) & mask;
        size_t firstDeleted = SIZE_MAX;

        for (size_t i = 0; i < capacity_; i++) {
            size_t pos = (idx + i) & mask;

            if (table_[pos].state == SlotState::Empty) {
                return (firstDeleted != SIZE_MAX) ? firstDeleted : pos;
            }
            if (table_[pos].state == SlotState::Deleted) {
                if (firstDeleted == SIZE_MAX) firstDeleted = pos;
                continue;
            }
            if (table_[pos].key == key) {
                return pos;
            }
        }

        return (firstDeleted != SIZE_MAX) ? firstDeleted : 0;
    }

    size_t findSlot(void* key) const {
        size_t mask = capacity_ - 1;
        size_t idx = hash(key) & mask;

        for (size_t i = 0; i < capacity_; i++) {
            size_t pos = (idx + i) & mask;
            if (table_[pos].state == SlotState::Empty) return SIZE_MAX;
            if (table_[pos].state == SlotState::Occupied &&
                table_[pos].key == key) {
                return pos;
            }
        }

        return SIZE_MAX;
    }

    void resize(size_t newCapacity) {
        // Must be power of 2
        newCapacity = nextPow2(newCapacity);

        std::vector<Slot> oldTable = std::move(table_);
        size_t oldCap = capacity_;

        table_.resize(newCapacity);
        capacity_ = newCapacity;
        size_ = 0;
        deleted_ = 0;

        for (size_t i = 0; i < oldCap; i++) {
            if (oldTable[i].state == SlotState::Occupied) {
                set(oldTable[i].key, oldTable[i].value);
            }
        }
    }

    void rehash() {
        resize(capacity_);
    }

    static size_t nextPow2(size_t n) {
        if (n == 0) return 1;
        n--;
        n |= n >> 1; n |= n >> 2; n |= n >> 4;
        n |= n >> 8; n |= n >> 16; n |= n >> 32;
        return n + 1;
    }

    std::vector<Slot> table_;
    size_t capacity_ = 0;
    size_t size_ = 0;
    size_t deleted_ = 0;
};

// =============================================================================
// Ephemeron Fixpoint Processor
// =============================================================================

/**
 * @brief Iterative fixpoint marking for ephemerons
 *
 * After main marking completes, ephemerons with unmarked keys
 * are in a "discovered" state. This processor iterates:
 * - If key is now marked (perhaps via another ephemeron's value),
 *   trace the value → may mark more objects → re-check
 * - Repeat until no new marks (fixpoint)
 * - Remaining ephemerons have truly dead keys
 */
class EphemeronFixpointProcessor {
public:
    struct Config {
        size_t maxIterations;       // Safety limit on fixpoint iterations
        Config() : maxIterations(1000) {}
    };

    struct Stats {
        size_t fixpointIterations;
        size_t entriesProcessed;
        size_t valuesTraced;
        size_t deadKeys;
        double processingMs;
    };

    using IsMarkedFn = std::function<bool(void*)>;
    using TraceFn = std::function<void(void*)>;  // Trace and mark an object

    explicit EphemeronFixpointProcessor(const Config& config = Config{})
        : config_(config) {}

    /**
     * @brief Add an ephemeron whose key was unmarked during main marking
     */
    void addDiscovered(void* key, void* value) {
        discovered_.push_back({key, value});
    }

    /**
     * @brief Run fixpoint iteration
     *
     * @param isMarked Checks if an object is marked
     * @param trace Traces an object (marks it and its references)
     * @return Stats about the fixpoint processing
     */
    Stats runFixpoint(IsMarkedFn isMarked, TraceFn trace) {
        Stats stats{};
        auto start = std::chrono::steady_clock::now();

        bool changed = true;
        size_t iteration = 0;

        while (changed && iteration < config_.maxIterations) {
            changed = false;
            iteration++;

            auto it = discovered_.begin();
            while (it != discovered_.end()) {
                stats.entriesProcessed++;

                if (isMarked(it->key)) {
                    // Key is now reachable → trace the value
                    if (it->value && !isMarked(it->value)) {
                        trace(it->value);
                        stats.valuesTraced++;
                        changed = true;
                    }
                    // Remove from discovered (fully processed)
                    it = discovered_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        stats.fixpointIterations = iteration;
        stats.deadKeys = discovered_.size();

        stats.processingMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        return stats;
    }

    /**
     * @brief Get entries with dead keys (call after fixpoint)
     */
    const std::vector<EphemeronEntry>& deadEntries() const {
        return discovered_;
    }

    /**
     * @brief Clear all state for next GC cycle
     */
    void reset() {
        discovered_.clear();
    }

    size_t discoveredCount() const { return discovered_.size(); }

private:
    Config config_;
    std::vector<EphemeronEntry> discovered_;
};

// =============================================================================
// Ephemeron Table Manager
// =============================================================================

/**
 * @brief Manages all WeakMap/WeakSet instances in the heap
 *
 * Each WeakMap JS object registers its backing hash table here.
 * During GC, the manager coordinates ephemeron processing:
 * - Registers ephemerons with the fixpoint processor
 * - Clears dead entries after fixpoint
 * - Reports dead entries to the weak processing pipeline
 */
class EphemeronTableManager {
public:
    struct Stats {
        size_t tablesRegistered;
        size_t totalEntries;
        size_t deadEntries;
        size_t fixpointIterations;
        double lastProcessingMs;
    };

    EphemeronTableManager() = default;

    /**
     * @brief Register a WeakMap's hash table
     */
    uint32_t registerTable(EphemeronHashTable* table) {
        uint32_t id = nextTableId_++;
        std::lock_guard<std::mutex> lock(mutex_);
        tables_[id] = table;
        return id;
    }

    void unregisterTable(uint32_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        tables_.erase(id);
    }

    /**
     * @brief Process all ephemeron tables after marking
     *
     * 1. Walk all tables, find entries with unmarked keys
     * 2. Add to fixpoint processor
     * 3. Run fixpoint
     * 4. Clear dead entries from tables
     */
    Stats processAfterMarking(
        std::function<bool(void*)> isMarked,
        std::function<void(void*)> trace
    ) {
        Stats stats{};
        auto start = std::chrono::steady_clock::now();

        EphemeronFixpointProcessor fixpoint;

        // Step 1: Discover ephemerons with unmarked keys
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [id, table] : tables_) {
                stats.totalEntries += table->size();

                table->forEach([&](void* key, void* value) {
                    if (isMarked(key)) {
                        // Key already marked → value is strong → trace it
                        if (value && !isMarked(value)) {
                            trace(value);
                        }
                    } else {
                        // Key unmarked → add to fixpoint
                        fixpoint.addDiscovered(key, value);
                    }
                });
            }
        }

        // Step 2: Run fixpoint
        auto fpStats = fixpoint.runFixpoint(isMarked, trace);
        stats.fixpointIterations = fpStats.fixpointIterations;
        stats.deadEntries = fpStats.deadKeys;

        // Step 3: Clear dead entries from all tables
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [id, table] : tables_) {
                // Collect keys to remove (can't modify during forEach)
                std::vector<void*> deadKeys;
                table->forEach([&](void* key, void* /*value*/) {
                    if (!isMarked(key)) {
                        deadKeys.push_back(key);
                    }
                });

                for (void* key : deadKeys) {
                    table->remove(key);
                }
            }
        }

        stats.tablesRegistered = tables_.size();
        stats.lastProcessingMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        return stats;
    }

    size_t tableCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tables_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, EphemeronHashTable*> tables_;
    uint32_t nextTableId_ = 1;
};

} // namespace Zepra::Heap
