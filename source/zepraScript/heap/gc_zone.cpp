// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_zone.cpp — Zone lifecycle, per-zone GC state, zone iteration

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <mutex>
#include <atomic>
#include <vector>
#include <functional>
#include <memory>

namespace Zepra::Heap {

enum class ZoneState : uint8_t {
    Idle,
    Marking,
    Sweeping,
    Compacting,
    Dead,
};

enum class ZoneType : uint8_t {
    Normal,
    Atoms,         // Shared interned strings
    SelfHosting,   // Browser-internal scripts
    System,        // Privileged system zone
};

struct ZoneStats {
    size_t totalAllocated;
    size_t totalFreed;
    size_t liveBytes;
    size_t arenaCount;
    size_t gcCount;
    double lastGCDurationMs;
    uint64_t allocationsSinceGC;

    ZoneStats() : totalAllocated(0), totalFreed(0), liveBytes(0), arenaCount(0)
        , gcCount(0), lastGCDurationMs(0), allocationsSinceGC(0) {}

    size_t fragmentation() const {
        return totalAllocated > 0 ? (totalAllocated - liveBytes) : 0;
    }

    double fragmentationRatio() const {
        return totalAllocated > 0 ? static_cast<double>(fragmentation()) / totalAllocated : 0;
    }
};

struct ZoneConfig {
    size_t initialArenaCapacity;
    size_t maxArenaCapacity;
    size_t gcTriggerBytes;        // Trigger GC after this many bytes allocated
    bool allowCompaction;
    ZoneType type;

    ZoneConfig()
        : initialArenaCapacity(16)
        , maxArenaCapacity(4096)
        , gcTriggerBytes(8 * 1024 * 1024)
        , allowCompaction(true)
        , type(ZoneType::Normal) {}

    static ZoneConfig atoms() {
        ZoneConfig c;
        c.type = ZoneType::Atoms;
        c.allowCompaction = false;
        c.gcTriggerBytes = 4 * 1024 * 1024;
        return c;
    }

    static ZoneConfig system() {
        ZoneConfig c;
        c.type = ZoneType::System;
        c.gcTriggerBytes = 16 * 1024 * 1024;
        return c;
    }
};

class Zone {
public:
    using ZoneId = uint32_t;

    explicit Zone(ZoneId id, const ZoneConfig& config = ZoneConfig{})
        : id_(id)
        , config_(config)
        , state_(ZoneState::Idle)
        , groupId_(0)
        , mallocBytes_(0)
        , mallocTrigger_(config.gcTriggerBytes)
        , keepAlive_(false)
        , sealed_(false) {}

    ~Zone() {
        state_ = ZoneState::Dead;
    }

    ZoneId id() const { return id_; }
    ZoneState state() const { return state_; }
    ZoneType type() const { return config_.type; }
    uint32_t groupId() const { return groupId_; }
    const ZoneConfig& config() const { return config_; }
    const ZoneStats& stats() const { return stats_; }

    void setState(ZoneState s) { state_ = s; }
    void setGroupId(uint32_t gid) { groupId_ = gid; }

    bool isCollecting() const {
        return state_ == ZoneState::Marking || state_ == ZoneState::Sweeping
            || state_ == ZoneState::Compacting;
    }

    bool isDead() const { return state_ == ZoneState::Dead; }
    bool isAtoms() const { return config_.type == ZoneType::Atoms; }
    bool isSystem() const { return config_.type == ZoneType::System; }

    // Track malloc-backed allocations for trigger calculation.
    void addMallocBytes(size_t bytes) {
        mallocBytes_.fetch_add(bytes, std::memory_order_relaxed);
        stats_.totalAllocated += bytes;
        stats_.allocationsSinceGC++;
    }

    void removeMallocBytes(size_t bytes) {
        size_t prev = mallocBytes_.load(std::memory_order_relaxed);
        size_t sub = bytes > prev ? prev : bytes;
        mallocBytes_.fetch_sub(sub, std::memory_order_relaxed);
        stats_.totalFreed += bytes;
    }

    size_t mallocBytes() const { return mallocBytes_.load(std::memory_order_relaxed); }

    bool shouldTriggerGC() const {
        if (sealed_) return false;
        return mallocBytes_.load(std::memory_order_relaxed) >= mallocTrigger_;
    }

    void setMallocTrigger(size_t bytes) { mallocTrigger_ = bytes; }

    void resetAllocationCounter() {
        mallocBytes_.store(0, std::memory_order_relaxed);
        stats_.allocationsSinceGC = 0;
    }

    // Arena tracking.
    void addArena() { stats_.arenaCount++; }
    void removeArena() { if (stats_.arenaCount > 0) stats_.arenaCount--; }

    void recordGCDuration(double ms) {
        stats_.gcCount++;
        stats_.lastGCDurationMs = ms;
    }

    void updateLiveBytes(size_t bytes) { stats_.liveBytes = bytes; }

    // Keep-alive prevents zone from being collected even when empty.
    void setKeepAlive(bool keep) { keepAlive_ = keep; }
    bool keepAlive() const { return keepAlive_; }

    // Sealed zones cannot trigger GC (used during bootstrap).
    void seal() { sealed_ = true; }
    void unseal() { sealed_ = false; }
    bool isSealed() const { return sealed_; }

    bool allowsCompaction() const { return config_.allowCompaction; }

private:
    ZoneId id_;
    ZoneConfig config_;
    ZoneState state_;
    uint32_t groupId_;
    std::atomic<size_t> mallocBytes_;
    size_t mallocTrigger_;
    ZoneStats stats_;
    bool keepAlive_;
    bool sealed_;
};

class ZoneRegistry {
public:
    Zone* createZone(const ZoneConfig& config = ZoneConfig{}) {
        std::lock_guard<std::mutex> lock(mutex_);
        Zone::ZoneId id = nextId_++;
        zones_.push_back(std::make_unique<Zone>(id, config));
        return zones_.back().get();
    }

    void destroyZone(Zone::ZoneId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = zones_.begin(); it != zones_.end(); ++it) {
            if ((*it)->id() == id) {
                (*it)->setState(ZoneState::Dead);
                zones_.erase(it);
                return;
            }
        }
    }

    Zone* findZone(Zone::ZoneId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& z : zones_) {
            if (z->id() == id) return z.get();
        }
        return nullptr;
    }

    template<typename Fn>
    void forEachZone(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& z : zones_) {
            if (!z->isDead()) fn(z.get());
        }
    }

    template<typename Fn>
    void forEachCollectingZone(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& z : zones_) {
            if (z->isCollecting()) fn(z.get());
        }
    }

    size_t zoneCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return zones_.size();
    }

    size_t totalMallocBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (auto& z : zones_) total += z->mallocBytes();
        return total;
    }

    bool anyZoneNeedsGC() const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& z : zones_) {
            if (z->shouldTriggerGC()) return true;
        }
        return false;
    }

    Zone* atomsZone() const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& z : zones_) {
            if (z->isAtoms()) return z.get();
        }
        return nullptr;
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Zone>> zones_;
    Zone::ZoneId nextId_ = 1;
};

} // namespace Zepra::Heap
