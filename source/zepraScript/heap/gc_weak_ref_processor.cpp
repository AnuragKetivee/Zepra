// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_weak_ref_processor.cpp — WeakRef/FinalizationRegistry host hook processing

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <mutex>
#include <vector>
#include <functional>
#include <algorithm>
#include <queue>

namespace Zepra::Heap {

struct WeakRefEntry {
    uint32_t id;
    void* target;          // The weakly-referenced cell
    void* weakRefCell;     // The WeakRef object itself
    bool cleared;

    WeakRefEntry() : id(0), target(nullptr), weakRefCell(nullptr), cleared(false) {}
};

struct FinalizationEntry {
    uint32_t registryId;
    void* target;          // The cell being observed
    void* heldValue;       // Value passed to cleanup callback
    void* unregisterToken; // Optional unregister token
    bool triggered;

    FinalizationEntry() : registryId(0), target(nullptr), heldValue(nullptr)
        , unregisterToken(nullptr), triggered(false) {}
};

class WeakRefProcessor {
public:
    struct Callbacks {
        std::function<bool(void* cell)> isMarked;
        std::function<void(void* weakRef)> clearWeakRef;
        std::function<void(void* heldValue, uint32_t registryId)> enqueueCleanup;
    };

    void setCallbacks(Callbacks cb) { cb_ = std::move(cb); }

    // Register a WeakRef for processing.
    uint32_t registerWeakRef(void* target, void* weakRefCell) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t id = nextId_++;
        WeakRefEntry entry;
        entry.id = id;
        entry.target = target;
        entry.weakRefCell = weakRefCell;
        entry.cleared = false;
        weakRefs_.push_back(entry);
        return id;
    }

    void unregisterWeakRef(uint32_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        weakRefs_.erase(
            std::remove_if(weakRefs_.begin(), weakRefs_.end(),
                [id](const WeakRefEntry& e) { return e.id == id; }),
            weakRefs_.end());
    }

    // Register a FinalizationRegistry entry.
    void registerFinalization(uint32_t registryId, void* target, void* heldValue,
                              void* unregisterToken = nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        FinalizationEntry entry;
        entry.registryId = registryId;
        entry.target = target;
        entry.heldValue = heldValue;
        entry.unregisterToken = unregisterToken;
        entry.triggered = false;
        finalizations_.push_back(entry);
    }

    void unregisterFinalization(void* unregisterToken) {
        std::lock_guard<std::mutex> lock(mutex_);
        finalizations_.erase(
            std::remove_if(finalizations_.begin(), finalizations_.end(),
                [unregisterToken](const FinalizationEntry& e) {
                    return e.unregisterToken == unregisterToken;
                }),
            finalizations_.end());
    }

    // Process after marking phase:
    // 1. Clear WeakRefs to unreachable targets.
    // 2. Enqueue finalization callbacks for dead targets.
    size_t processAfterMarking() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t processed = 0;

        // Clear dead WeakRefs.
        for (auto& ref : weakRefs_) {
            if (ref.cleared) continue;
            if (!ref.target) continue;

            if (!cb_.isMarked(ref.target)) {
                if (cb_.clearWeakRef) cb_.clearWeakRef(ref.weakRefCell);
                ref.target = nullptr;
                ref.cleared = true;
                processed++;
                stats_.weakRefsCleared++;
            }
        }

        // Enqueue finalization callbacks.
        for (auto& fin : finalizations_) {
            if (fin.triggered) continue;
            if (!fin.target) continue;

            if (!cb_.isMarked(fin.target)) {
                if (cb_.enqueueCleanup) {
                    cb_.enqueueCleanup(fin.heldValue, fin.registryId);
                }
                fin.target = nullptr;
                fin.triggered = true;
                processed++;
                stats_.finalizationsTriggered++;
            }
        }

        // Clean up triggered entries.
        finalizations_.erase(
            std::remove_if(finalizations_.begin(), finalizations_.end(),
                [](const FinalizationEntry& e) { return e.triggered; }),
            finalizations_.end());

        // Clean up cleared weak refs.
        weakRefs_.erase(
            std::remove_if(weakRefs_.begin(), weakRefs_.end(),
                [](const WeakRefEntry& e) { return e.cleared; }),
            weakRefs_.end());

        return processed;
    }

    size_t weakRefCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return weakRefs_.size();
    }

    size_t finalizationCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return finalizations_.size();
    }

    struct Stats {
        uint64_t weakRefsCleared = 0;
        uint64_t finalizationsTriggered = 0;
    };

    const Stats& stats() const { return stats_; }

private:
    mutable std::mutex mutex_;
    std::vector<WeakRefEntry> weakRefs_;
    std::vector<FinalizationEntry> finalizations_;
    uint32_t nextId_ = 1;
    Callbacks cb_;
    Stats stats_;
};

} // namespace Zepra::Heap
