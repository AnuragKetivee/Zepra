// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_destructor_dispatch.cpp — Type-indexed destructor table, ordering, safety

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <mutex>
#include <vector>
#include <functional>
#include <algorithm>
#include <unordered_map>
#include <atomic>

namespace Zepra::Heap {

enum class CellType : uint8_t;  // From gc_cell.cpp

struct DestructorInfo {
    CellType cellType;
    std::function<void(void* cell)> destructor;
    uint16_t priority;         // Lower = runs first
    bool requiresSafepoint;
    bool isIdempotent;         // Can be called multiple times safely
    const char* typeName;

    DestructorInfo() : cellType{}, priority(100), requiresSafepoint(false)
        , isIdempotent(false), typeName("") {}
};

class DestructorDispatch {
public:
    static constexpr size_t kMaxCellTypes = 32;

    // Register a destructor for a cell type.
    void registerDestructor(CellType type, std::function<void(void* cell)> dtor,
                            uint16_t priority = 100, bool needsSafepoint = false,
                            bool idempotent = false, const char* name = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t idx = static_cast<size_t>(type);
        assert(idx < kMaxCellTypes);

        table_[idx].cellType = type;
        table_[idx].destructor = std::move(dtor);
        table_[idx].priority = priority;
        table_[idx].requiresSafepoint = needsSafepoint;
        table_[idx].isIdempotent = idempotent;
        table_[idx].typeName = name;
        registered_[idx] = true;
    }

    bool hasDestructor(CellType type) const {
        size_t idx = static_cast<size_t>(type);
        return idx < kMaxCellTypes && registered_[idx];
    }

    // Call destructor for a single cell.
    bool destruct(void* cell, CellType type) {
        size_t idx = static_cast<size_t>(type);
        if (idx >= kMaxCellTypes || !registered_[idx] || !table_[idx].destructor) {
            return false;
        }

        table_[idx].destructor(cell);
        stats_.totalDestructions.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // Batch destruction: sort by priority, call in order.
    struct BatchItem {
        void* cell;
        CellType type;
    };

    size_t destructBatch(std::vector<BatchItem>& items) {
        // Sort by destructor priority (lower runs first).
        std::sort(items.begin(), items.end(),
            [this](const BatchItem& a, const BatchItem& b) {
                size_t ai = static_cast<size_t>(a.type);
                size_t bi = static_cast<size_t>(b.type);
                uint16_t ap = ai < kMaxCellTypes ? table_[ai].priority : 100;
                uint16_t bp = bi < kMaxCellTypes ? table_[bi].priority : 100;
                return ap < bp;
            });

        // Separate into safepoint-required and non-safepoint.
        std::vector<BatchItem> safepointItems;
        std::vector<BatchItem> normalItems;

        for (auto& item : items) {
            size_t idx = static_cast<size_t>(item.type);
            if (idx < kMaxCellTypes && table_[idx].requiresSafepoint) {
                safepointItems.push_back(item);
            } else {
                normalItems.push_back(item);
            }
        }

        size_t destroyed = 0;

        // Run non-safepoint destructors first.
        for (auto& item : normalItems) {
            if (destruct(item.cell, item.type)) destroyed++;
        }

        // Run safepoint-required destructors.
        for (auto& item : safepointItems) {
            if (destruct(item.cell, item.type)) destroyed++;
        }

        return destroyed;
    }

    // Safety check: verify no destructor calls into GC.
    void beginDestructionPhase() {
        inDestructionPhase_ = true;
    }

    void endDestructionPhase() {
        inDestructionPhase_ = false;
    }

    bool isInDestructionPhase() const { return inDestructionPhase_; }

    // Unregister all destructors (shutdown).
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < kMaxCellTypes; i++) {
            table_[i] = {};
            registered_[i] = false;
        }
    }

    size_t registeredCount() const {
        size_t count = 0;
        for (size_t i = 0; i < kMaxCellTypes; i++) {
            if (registered_[i]) count++;
        }
        return count;
    }

    struct Stats {
        std::atomic<uint64_t> totalDestructions{0};
    };

    const Stats& stats() const { return stats_; }

private:
    std::mutex mutex_;
    DestructorInfo table_[kMaxCellTypes];
    bool registered_[kMaxCellTypes] = {};
    bool inDestructionPhase_ = false;
    Stats stats_;
};

} // namespace Zepra::Heap
