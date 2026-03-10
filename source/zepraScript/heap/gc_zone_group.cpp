// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_zone_group.cpp — Zone grouping for concurrent collection

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <atomic>

namespace Zepra::Heap {

class Zone;  // Forward declaration

struct ZoneGroupStats {
    size_t zoneCount;
    size_t totalBytes;
    size_t liveBytes;
    uint64_t gcCount;

    ZoneGroupStats() : zoneCount(0), totalBytes(0), liveBytes(0), gcCount(0) {}
};

enum class GroupCollectionState : uint8_t {
    Idle,
    ScheduledForCollection,
    Marking,
    Sweeping,
    Complete,
};

class ZoneGroup {
public:
    using GroupId = uint32_t;

    explicit ZoneGroup(GroupId id) : id_(id), state_(GroupCollectionState::Idle)
        , priority_(0), lastCollectedMs_(0) {}

    GroupId id() const { return id_; }
    GroupCollectionState state() const { return state_; }
    void setState(GroupCollectionState s) { state_ = s; }

    void addZone(Zone* zone) {
        zones_.push_back(zone);
        stats_.zoneCount = zones_.size();
    }

    void removeZone(Zone* zone) {
        zones_.erase(std::remove(zones_.begin(), zones_.end(), zone), zones_.end());
        stats_.zoneCount = zones_.size();
    }

    bool containsZone(const Zone* zone) const {
        return std::find(zones_.begin(), zones_.end(), zone) != zones_.end();
    }

    size_t zoneCount() const { return zones_.size(); }
    bool empty() const { return zones_.empty(); }

    template<typename Fn>
    void forEachZone(Fn&& fn) {
        for (auto* z : zones_) fn(z);
    }

    template<typename Fn>
    void forEachZone(Fn&& fn) const {
        for (const auto* z : zones_) fn(z);
    }

    void setPriority(uint32_t p) { priority_ = p; }
    uint32_t priority() const { return priority_; }

    void recordCollection(uint64_t timestampMs) {
        lastCollectedMs_ = timestampMs;
        stats_.gcCount++;
        state_ = GroupCollectionState::Complete;
    }

    uint64_t lastCollectedMs() const { return lastCollectedMs_; }
    const ZoneGroupStats& stats() const { return stats_; }

    void updateStats(size_t totalBytes, size_t liveBytes) {
        stats_.totalBytes = totalBytes;
        stats_.liveBytes = liveBytes;
    }

    bool isCollecting() const {
        return state_ == GroupCollectionState::Marking
            || state_ == GroupCollectionState::Sweeping;
    }

    bool isScheduled() const {
        return state_ == GroupCollectionState::ScheduledForCollection;
    }

private:
    GroupId id_;
    GroupCollectionState state_;
    uint32_t priority_;
    uint64_t lastCollectedMs_;
    std::vector<Zone*> zones_;
    ZoneGroupStats stats_;
};

class ZoneGroupManager {
public:
    ZoneGroup* createGroup() {
        std::lock_guard<std::mutex> lock(mutex_);
        ZoneGroup::GroupId id = nextGroupId_++;
        groups_.push_back(std::make_unique<ZoneGroup>(id));
        return groups_.back().get();
    }

    void destroyGroup(ZoneGroup::GroupId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        groups_.erase(
            std::remove_if(groups_.begin(), groups_.end(),
                [id](const auto& g) { return g->id() == id; }),
            groups_.end());
    }

    ZoneGroup* findGroup(ZoneGroup::GroupId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& g : groups_) {
            if (g->id() == id) return g.get();
        }
        return nullptr;
    }

    ZoneGroup* groupForZone(const Zone* zone) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& g : groups_) {
            if (g->containsZone(zone)) return g.get();
        }
        return nullptr;
    }

    void assignZoneToGroup(Zone* zone, ZoneGroup::GroupId groupId) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Remove from any existing group first.
        for (auto& g : groups_) g->removeZone(zone);
        // Add to target group.
        for (auto& g : groups_) {
            if (g->id() == groupId) {
                g->addZone(zone);
                return;
            }
        }
    }

    // Auto-group zones by cross-zone reference analysis.
    void rebuildGroups(const std::vector<Zone*>& allZones,
                       std::function<bool(const Zone*, const Zone*)> hasReference) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Clear existing groups.
        groups_.clear();

        // Union-find to group connected zones.
        std::unordered_map<const Zone*, const Zone*> parent;
        for (auto* z : allZones) parent[z] = z;

        auto find = [&](const Zone* z) -> const Zone* {
            while (parent[z] != z) {
                parent[z] = parent[parent[z]];
                z = parent[z];
            }
            return z;
        };

        auto unite = [&](const Zone* a, const Zone* b) {
            const Zone* ra = find(a);
            const Zone* rb = find(b);
            if (ra != rb) parent[ra] = rb;
        };

        // Build union-find based on references.
        for (size_t i = 0; i < allZones.size(); i++) {
            for (size_t j = i + 1; j < allZones.size(); j++) {
                if (hasReference(allZones[i], allZones[j]) ||
                    hasReference(allZones[j], allZones[i])) {
                    unite(allZones[i], allZones[j]);
                }
            }
        }

        // Build groups from union-find.
        std::unordered_map<const Zone*, ZoneGroup*> rootToGroup;
        for (auto* z : allZones) {
            const Zone* root = find(z);
            if (rootToGroup.find(root) == rootToGroup.end()) {
                ZoneGroup::GroupId id = nextGroupId_++;
                groups_.push_back(std::make_unique<ZoneGroup>(id));
                rootToGroup[root] = groups_.back().get();
            }
            rootToGroup[root]->addZone(const_cast<Zone*>(z));
        }
    }

    // Schedule groups for collection based on byte threshold or priority.
    void scheduleCollections(size_t bytesThreshold) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& g : groups_) {
            if (g->stats().totalBytes >= bytesThreshold && !g->isCollecting()) {
                g->setState(GroupCollectionState::ScheduledForCollection);
            }
        }
    }

    // Get all groups scheduled for collection, sorted by priority.
    std::vector<ZoneGroup*> scheduledGroups() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ZoneGroup*> result;
        for (auto& g : groups_) {
            if (g->isScheduled()) result.push_back(g.get());
        }
        std::sort(result.begin(), result.end(),
            [](const ZoneGroup* a, const ZoneGroup* b) {
                return a->priority() > b->priority();
            });
        return result;
    }

    size_t groupCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return groups_.size();
    }

    template<typename Fn>
    void forEachGroup(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& g : groups_) fn(g.get());
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<ZoneGroup>> groups_;
    ZoneGroup::GroupId nextGroupId_ = 1;
};

} // namespace Zepra::Heap
