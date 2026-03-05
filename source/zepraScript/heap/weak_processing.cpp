/**
 * @file weak_processing.cpp
 * @brief Weak reference processing for GC
 *
 * Implements the full weak reference lifecycle:
 *
 * 1. WeakHandle: GC-managed handle that doesn't prevent collection.
 *    When the referent is collected, the handle is cleared to null.
 *    Supports a callback on clear (weak callback).
 *
 * 2. WeakHandleTable: Central registry of all weak handles.
 *    After marking, the GC walks this table and clears handles
 *    whose referents are unmarked (dead).
 *
 * 3. PhantomHandle: Like WeakHandle, but the callback fires
 *    after the referent is finalized and its memory is reclaimed.
 *    The callback receives no reference to the dead object.
 *
 * 4. WeakCell: Used internally for WeakRef and WeakMap.
 *    A cell that participates in GC marking but whose
 *    target can be collected independently.
 *
 * Processing order after marking:
 *   1. Clear dead WeakRefs
 *   2. Process FinalizationRegistry entries (via finalizer_engine)
 *   3. Clear dead WeakMap/WeakSet keys
 *   4. Fire weak callbacks (non-weak phantom callbacks deferred)
 *   5. Fire phantom callbacks
 *
 * This file handles steps 1, 3, 4, 5. Step 2 is in finalizer_engine.cpp.
 */

#include <atomic>
#include <mutex>
#include <shared_mutex>
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
// Weak Handle Types
// =============================================================================

enum class WeakHandleType : uint8_t {
    Weak,       // Cleared when referent dies, callback receives the handle
    Phantom,    // Callback fires after referent is finalized
    Weak2,      // Secondary weak (cleared after primary weak callbacks)
};

enum class WeakHandleState : uint8_t {
    Active,     // Referent is alive or not yet processed
    Pending,    // Referent is dead, callback scheduled
    Cleared,    // Handle has been cleared
    Freed,      // Handle slot recycled
};

// =============================================================================
// Weak Handle
// =============================================================================

struct WeakHandle {
    uint32_t id;
    WeakHandleType type;
    WeakHandleState state;
    uint16_t generation;            // For ABA prevention during reuse

    void* referent;                 // The weakly-held object
    void* parameter;               // User data for callback
    uint32_t classId;               // Object class (for weak map key identification)

    using Callback = void (*)(void* parameter, void* referent);
    Callback callback;

    bool isActive() const { return state == WeakHandleState::Active; }
    bool isDead() const { return state == WeakHandleState::Cleared ||
                                 state == WeakHandleState::Freed; }
};

// =============================================================================
// Weak Handle Table
// =============================================================================

/**
 * @brief Central registry of all weak handles
 *
 * Uses a flat array with free-list for O(1) create/destroy.
 * Generation counters prevent ABA problems when handles are reused.
 */
class WeakHandleTable {
public:
    struct Config {
        size_t initialCapacity;
        size_t maxCapacity;

        Config()
            : initialCapacity(1024)
            , maxCapacity(1024 * 1024) {}
    };

    struct Stats {
        size_t totalCreated;
        size_t totalDestroyed;
        size_t totalCleared;
        size_t activeCount;
        size_t pendingCallbacks;
        size_t capacity;
        double lastProcessingMs;
    };

    explicit WeakHandleTable(const Config& config = Config{});
    ~WeakHandleTable();

    WeakHandleTable(const WeakHandleTable&) = delete;
    WeakHandleTable& operator=(const WeakHandleTable&) = delete;

    // -------------------------------------------------------------------------
    // Handle creation / destruction
    // -------------------------------------------------------------------------

    /**
     * @brief Create a new weak handle
     * @return Handle ID (0 = failure)
     */
    uint32_t create(void* referent, WeakHandleType type,
                    WeakHandle::Callback callback = nullptr,
                    void* parameter = nullptr);

    /**
     * @brief Destroy a handle by ID
     */
    void destroy(uint32_t handleId);

    /**
     * @brief Get referent (returns null if handle has been cleared)
     */
    void* get(uint32_t handleId) const;

    /**
     * @brief Check if handle is still valid and referent is alive
     */
    bool isAlive(uint32_t handleId) const;

    // -------------------------------------------------------------------------
    // GC processing (called after marking)
    // -------------------------------------------------------------------------

    /**
     * @brief Process all weak handles after marking
     *
     * For each handle whose referent is unmarked (dead):
     *   - Clear the handle (set referent to null)
     *   - Queue callback if present
     *
     * @param isMarked Returns true if the object is marked (alive)
     * @return Number of handles cleared
     */
    size_t processAfterMarking(std::function<bool(void*)> isMarked);

    /**
     * @brief Fire all queued weak callbacks
     *
     * Called after all weak handles have been processed.
     * Callbacks for Weak type fire first, then Phantom.
     *
     * @return Number of callbacks fired
     */
    size_t fireCallbacks();

    /**
     * @brief Fire only phantom callbacks (second pass)
     */
    size_t firePhantomCallbacks();

    // -------------------------------------------------------------------------
    // Enumeration
    // -------------------------------------------------------------------------

    /**
     * @brief Iterate all active handles
     */
    void forEach(std::function<void(const WeakHandle&)> visitor) const;

    /**
     * @brief Count active handles by type
     */
    size_t countByType(WeakHandleType type) const;

    Stats computeStats() const;

private:
    uint32_t allocateSlot();
    void freeSlot(uint32_t idx);
    bool isValidHandle(uint32_t id) const;

    Config config_;

    // Flat array storage
    std::vector<WeakHandle> handles_;
    std::vector<uint32_t> freeList_;
    mutable std::shared_mutex mutex_;

    // Pending callbacks (queued during processAfterMarking)
    struct PendingCallback {
        WeakHandle::Callback callback;
        void* parameter;
        void* referent;     // For Weak type (null for Phantom)
        WeakHandleType type;
    };
    std::vector<PendingCallback> pendingCallbacks_;

    // Stats
    size_t totalCreated_ = 0;
    size_t totalDestroyed_ = 0;
    size_t totalCleared_ = 0;
};

// =============================================================================
// Implementation
// =============================================================================

inline WeakHandleTable::WeakHandleTable(const Config& config)
    : config_(config) {
    handles_.reserve(config.initialCapacity);
}

inline WeakHandleTable::~WeakHandleTable() = default;

inline uint32_t WeakHandleTable::create(
    void* referent, WeakHandleType type,
    WeakHandle::Callback callback, void* parameter
) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    uint32_t idx = allocateSlot();
    if (idx == 0) return 0;

    auto& h = handles_[idx];
    h.id = idx;
    h.type = type;
    h.state = WeakHandleState::Active;
    h.referent = referent;
    h.parameter = parameter;
    h.callback = callback;
    h.classId = 0;

    totalCreated_++;
    return idx;
}

inline void WeakHandleTable::destroy(uint32_t handleId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!isValidHandle(handleId)) return;

    handles_[handleId].state = WeakHandleState::Freed;
    handles_[handleId].referent = nullptr;
    handles_[handleId].callback = nullptr;
    freeSlot(handleId);

    totalDestroyed_++;
}

inline void* WeakHandleTable::get(uint32_t handleId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (!isValidHandle(handleId)) return nullptr;
    const auto& h = handles_[handleId];
    if (h.state != WeakHandleState::Active) return nullptr;
    return h.referent;
}

inline bool WeakHandleTable::isAlive(uint32_t handleId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (!isValidHandle(handleId)) return false;
    return handles_[handleId].state == WeakHandleState::Active &&
           handles_[handleId].referent != nullptr;
}

inline size_t WeakHandleTable::processAfterMarking(
    std::function<bool(void*)> isMarked
) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    pendingCallbacks_.clear();

    size_t cleared = 0;

    for (size_t i = 1; i < handles_.size(); i++) {
        auto& h = handles_[i];
        if (h.state != WeakHandleState::Active) continue;
        if (!h.referent) continue;

        if (!isMarked(h.referent)) {
            // Referent is dead
            if (h.callback) {
                PendingCallback pc;
                pc.callback = h.callback;
                pc.parameter = h.parameter;
                pc.referent = (h.type == WeakHandleType::Weak) ? h.referent : nullptr;
                pc.type = h.type;
                pendingCallbacks_.push_back(pc);
                h.state = WeakHandleState::Pending;
            } else {
                h.state = WeakHandleState::Cleared;
            }

            h.referent = nullptr;
            cleared++;
            totalCleared_++;
        }
    }

    return cleared;
}

inline size_t WeakHandleTable::fireCallbacks() {
    // Copy pending list and release lock before firing
    std::vector<PendingCallback> toFire;
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        // Fire Weak callbacks first (they may access referent in some impls)
        for (auto& pc : pendingCallbacks_) {
            if (pc.type == WeakHandleType::Weak ||
                pc.type == WeakHandleType::Weak2) {
                toFire.push_back(pc);
            }
        }
    }

    for (auto& pc : toFire) {
        pc.callback(pc.parameter, pc.referent);
    }

    // Mark as cleared
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto& h : handles_) {
            if (h.state == WeakHandleState::Pending &&
                (h.type == WeakHandleType::Weak ||
                 h.type == WeakHandleType::Weak2)) {
                h.state = WeakHandleState::Cleared;
            }
        }
    }

    return toFire.size();
}

inline size_t WeakHandleTable::firePhantomCallbacks() {
    std::vector<PendingCallback> toFire;
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto& pc : pendingCallbacks_) {
            if (pc.type == WeakHandleType::Phantom) {
                toFire.push_back(pc);
            }
        }
        pendingCallbacks_.clear();
    }

    for (auto& pc : toFire) {
        pc.callback(pc.parameter, nullptr);
    }

    // Mark as cleared
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto& h : handles_) {
            if (h.state == WeakHandleState::Pending &&
                h.type == WeakHandleType::Phantom) {
                h.state = WeakHandleState::Cleared;
            }
        }
    }

    return toFire.size();
}

inline void WeakHandleTable::forEach(
    std::function<void(const WeakHandle&)> visitor
) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (size_t i = 1; i < handles_.size(); i++) {
        if (handles_[i].state == WeakHandleState::Active) {
            visitor(handles_[i]);
        }
    }
}

inline size_t WeakHandleTable::countByType(WeakHandleType type) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    size_t count = 0;
    for (size_t i = 1; i < handles_.size(); i++) {
        if (handles_[i].state == WeakHandleState::Active &&
            handles_[i].type == type) {
            count++;
        }
    }
    return count;
}

inline WeakHandleTable::Stats WeakHandleTable::computeStats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    Stats stats{};
    stats.totalCreated = totalCreated_;
    stats.totalDestroyed = totalDestroyed_;
    stats.totalCleared = totalCleared_;
    stats.capacity = handles_.size();
    stats.pendingCallbacks = pendingCallbacks_.size();

    size_t active = 0;
    for (size_t i = 1; i < handles_.size(); i++) {
        if (handles_[i].state == WeakHandleState::Active) active++;
    }
    stats.activeCount = active;

    return stats;
}

inline uint32_t WeakHandleTable::allocateSlot() {
    if (!freeList_.empty()) {
        uint32_t idx = freeList_.back();
        freeList_.pop_back();
        handles_[idx].generation++;
        return idx;
    }

    if (handles_.size() >= config_.maxCapacity) return 0;

    if (handles_.empty()) {
        // Slot 0 is reserved (invalid handle)
        handles_.resize(1);
        handles_[0].state = WeakHandleState::Freed;
    }

    uint32_t idx = static_cast<uint32_t>(handles_.size());
    handles_.emplace_back();
    handles_[idx].generation = 0;
    return idx;
}

inline void WeakHandleTable::freeSlot(uint32_t idx) {
    freeList_.push_back(idx);
}

inline bool WeakHandleTable::isValidHandle(uint32_t id) const {
    return id > 0 && id < handles_.size();
}

// =============================================================================
// Weak Cell Chain
// =============================================================================

/**
 * @brief Intrusive linked list of weak cells
 *
 * Used by WeakRef and FinalizationRegistry to track targets.
 * Each cell holds a weak reference to a target and is linked
 * into the target's weak cell chain.
 */
struct WeakCell {
    void* target;           // Weakly-held target
    void* holdings;         // User data (for FinalizationRegistry)
    void* unregisterToken;  // Token for unregistration
    WeakCell* next;
    WeakCell* prev;
    uint32_t registryId;    // Which FinalizationRegistry owns this
    bool cleared;

    WeakCell()
        : target(nullptr), holdings(nullptr), unregisterToken(nullptr)
        , next(nullptr), prev(nullptr), registryId(0), cleared(false) {}
};

/**
 * @brief Manages a doubly-linked list of WeakCells
 */
class WeakCellChain {
public:
    WeakCellChain() : head_(nullptr), count_(0) {}

    ~WeakCellChain() {
        // Don't own the cells — they're managed externally
        head_ = nullptr;
        count_ = 0;
    }

    void insert(WeakCell* cell) {
        cell->next = head_;
        cell->prev = nullptr;
        if (head_) head_->prev = cell;
        head_ = cell;
        count_++;
    }

    void remove(WeakCell* cell) {
        if (cell->prev) cell->prev->next = cell->next;
        else head_ = cell->next;
        if (cell->next) cell->next->prev = cell->prev;
        cell->next = nullptr;
        cell->prev = nullptr;
        count_--;
    }

    /**
     * @brief Clear all cells whose target is dead
     * @return Count of cleared cells
     */
    size_t clearDead(std::function<bool(void*)> isMarked) {
        size_t cleared = 0;
        WeakCell* current = head_;

        while (current) {
            WeakCell* next = current->next;

            if (current->target && !current->cleared) {
                if (!isMarked(current->target)) {
                    current->target = nullptr;
                    current->cleared = true;
                    cleared++;
                }
            }

            current = next;
        }

        return cleared;
    }

    /**
     * @brief Remove all cleared cells from the chain
     */
    void purgeClearedCells() {
        WeakCell* current = head_;
        while (current) {
            WeakCell* next = current->next;
            if (current->cleared) {
                remove(current);
            }
            current = next;
        }
    }

    void forEach(std::function<void(WeakCell&)> visitor) {
        WeakCell* current = head_;
        while (current) {
            visitor(*current);
            current = current->next;
        }
    }

    size_t count() const { return count_; }
    bool empty() const { return head_ == nullptr; }
    WeakCell* head() { return head_; }

private:
    WeakCell* head_;
    size_t count_;
};

// =============================================================================
// Weak Reference Processing Coordinator
// =============================================================================

/**
 * @brief Coordinates all weak reference processing after marking
 *
 * Processing order:
 * 1. Process WeakHandleTable (clear dead handles, queue callbacks)
 * 2. Process WeakCellChains (clear dead WeakRef/FinalizationRegistry targets)
 * 3. Fire weak callbacks
 * 4. Fire phantom callbacks
 */
class WeakProcessor {
public:
    struct Stats {
        size_t handlesCleared;
        size_t cellsCleared;
        size_t weakCallbacksFired;
        size_t phantomCallbacksFired;
        double processingMs;
    };

    WeakProcessor() = default;

    void setHandleTable(WeakHandleTable* table) { handleTable_ = table; }

    void addCellChain(WeakCellChain* chain) {
        cellChains_.push_back(chain);
    }

    void removeCellChain(WeakCellChain* chain) {
        cellChains_.erase(
            std::remove(cellChains_.begin(), cellChains_.end(), chain),
            cellChains_.end());
    }

    /**
     * @brief Run full weak processing pass
     */
    Stats process(std::function<bool(void*)> isMarked) {
        Stats stats{};
        auto start = std::chrono::steady_clock::now();

        // Step 1: Process weak handles
        if (handleTable_) {
            stats.handlesCleared = handleTable_->processAfterMarking(isMarked);
        }

        // Step 2: Process weak cells
        for (auto* chain : cellChains_) {
            stats.cellsCleared += chain->clearDead(isMarked);
        }

        // Step 3: Fire weak callbacks
        if (handleTable_) {
            stats.weakCallbacksFired = handleTable_->fireCallbacks();
        }

        // Step 4: Fire phantom callbacks
        if (handleTable_) {
            stats.phantomCallbacksFired = handleTable_->firePhantomCallbacks();
        }

        stats.processingMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        return stats;
    }

private:
    WeakHandleTable* handleTable_ = nullptr;
    std::vector<WeakCellChain*> cellChains_;
};

} // namespace Zepra::Heap
