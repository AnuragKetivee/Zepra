// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_weak_map.cpp — Ephemeron-based weak map with iterative marking fixpoint

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <algorithm>

namespace Zepra::Heap {

// An ephemeron is a key-value pair where the value is only reachable
// if the key is also reachable through non-ephemeron paths.
struct Ephemeron {
    void* key;
    void* value;
    bool keyMarked;
    bool processed;

    Ephemeron() : key(nullptr), value(nullptr), keyMarked(false), processed(false) {}
    Ephemeron(void* k, void* v) : key(k), value(v), keyMarked(false), processed(false) {}
};

class WeakMapGC {
public:
    using MapId = uint32_t;

    struct Callbacks {
        std::function<bool(void* cell)> isMarked;
        std::function<void(void* cell)> markCell;
        std::function<void(void* cell)> traceCell;  // Full recursive trace
    };

    void setCallbacks(Callbacks cb) { cb_ = std::move(cb); }

    // Register a WeakMap for GC tracking.
    MapId registerMap(void* mapCell) {
        std::lock_guard<std::mutex> lock(mutex_);
        MapId id = nextId_++;
        WeakMapEntry entry;
        entry.id = id;
        entry.mapCell = mapCell;
        maps_[id] = entry;
        return id;
    }

    void unregisterMap(MapId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        maps_.erase(id);
    }

    // Add an ephemeron to a WeakMap (called during marking traversal).
    void addEphemeron(MapId mapId, void* key, void* value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = maps_.find(mapId);
        if (it == maps_.end()) return;
        it->second.ephemerons.push_back({key, value});
    }

    // Iterative ephemeron fixpoint: repeat until no new marks.
    // This resolves chains where marking a value exposes new reachable keys.
    size_t resolveFixpoint() {
        size_t totalMarked = 0;
        size_t iterations = 0;
        bool progress = true;

        while (progress) {
            progress = false;
            iterations++;

            std::lock_guard<std::mutex> lock(mutex_);

            for (auto& [id, entry] : maps_) {
                // WeakMap itself must be reachable.
                if (!cb_.isMarked(entry.mapCell)) continue;

                for (auto& eph : entry.ephemerons) {
                    if (eph.processed) continue;

                    if (cb_.isMarked(eph.key)) {
                        // Key is reachable — mark the value.
                        if (!cb_.isMarked(eph.value)) {
                            cb_.markCell(eph.value);
                            if (cb_.traceCell) cb_.traceCell(eph.value);
                            totalMarked++;
                            progress = true;
                        }
                        eph.keyMarked = true;
                        eph.processed = true;
                    }
                }
            }
        }

        stats_.fixpointIterations += iterations;
        stats_.totalMarkedViaEphemeron += totalMarked;
        return totalMarked;
    }

    // Sweep: remove entries with unreachable keys.
    size_t sweep() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t removed = 0;

        for (auto& [id, entry] : maps_) {
            if (!cb_.isMarked(entry.mapCell)) continue;

            auto& ephs = entry.ephemerons;
            auto newEnd = std::remove_if(ephs.begin(), ephs.end(),
                [this](const Ephemeron& e) {
                    return !cb_.isMarked(e.key);
                });
            removed += std::distance(newEnd, ephs.end());
            ephs.erase(newEnd, ephs.end());
        }

        // Remove dead maps.
        for (auto it = maps_.begin(); it != maps_.end(); ) {
            if (!cb_.isMarked(it->second.mapCell)) {
                removed += it->second.ephemerons.size();
                it = maps_.erase(it);
            } else {
                ++it;
            }
        }

        stats_.totalSwept += removed;
        return removed;
    }

    // Reset ephemeron state for new marking cycle.
    void prepareForMarking() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, entry] : maps_) {
            for (auto& eph : entry.ephemerons) {
                eph.keyMarked = false;
                eph.processed = false;
            }
        }
    }

    size_t mapCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return maps_.size();
    }

    size_t totalEphemerons() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (auto& [id, entry] : maps_) total += entry.ephemerons.size();
        return total;
    }

    struct Stats {
        uint64_t fixpointIterations = 0;
        uint64_t totalMarkedViaEphemeron = 0;
        uint64_t totalSwept = 0;
    };

    const Stats& stats() const { return stats_; }

private:
    struct WeakMapEntry {
        MapId id;
        void* mapCell;
        std::vector<Ephemeron> ephemerons;
    };

    mutable std::mutex mutex_;
    std::unordered_map<MapId, WeakMapEntry> maps_;
    MapId nextId_ = 1;
    Callbacks cb_;
    Stats stats_;
};

} // namespace Zepra::Heap
