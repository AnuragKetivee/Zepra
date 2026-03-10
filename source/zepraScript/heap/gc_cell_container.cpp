// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_cell_container.cpp — Container holding cells (arena or LOS block)

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>

namespace Zepra::Heap {

enum class ContainerKind : uint8_t {
    Arena,         // Fixed-size cells in a 16KB arena
    LargeObject,   // Single large object in its own allocation
    CodeBlock,     // Executable code (W^X protected)
};

struct ContainerHeader {
    ContainerKind kind;
    uint8_t flags;
    uint16_t sizeClass;
    uint32_t zoneId;
    uint32_t cellCount;
    uint32_t liveCells;
    uintptr_t baseAddress;
    size_t totalSize;

    static constexpr uint8_t kFlagSweepable   = 0x01;
    static constexpr uint8_t kFlagCompactable  = 0x02;
    static constexpr uint8_t kFlagExecutable   = 0x04;
    static constexpr uint8_t kFlagDecommitted  = 0x08;

    bool isSweepable() const { return flags & kFlagSweepable; }
    bool isCompactable() const { return flags & kFlagCompactable; }
    bool isExecutable() const { return flags & kFlagExecutable; }
    bool isDecommitted() const { return flags & kFlagDecommitted; }

    void setFlag(uint8_t f) { flags |= f; }
    void clearFlag(uint8_t f) { flags &= ~f; }

    double occupancy() const {
        return cellCount > 0 ? static_cast<double>(liveCells) / cellCount : 0;
    }

    bool isEmpty() const { return liveCells == 0; }
    bool isFull() const { return liveCells == cellCount; }
};

class CellContainer {
public:
    explicit CellContainer(ContainerKind kind, uintptr_t base, size_t size,
                           uint16_t sizeClass, uint32_t zoneId)
        : base_(base), size_(size) {
        header_.kind = kind;
        header_.flags = ContainerHeader::kFlagSweepable;
        header_.sizeClass = sizeClass;
        header_.zoneId = zoneId;
        header_.cellCount = sizeClass > 0 ? static_cast<uint32_t>(size / sizeClass) : 1;
        header_.liveCells = 0;
        header_.baseAddress = base;
        header_.totalSize = size;

        if (kind == ContainerKind::Arena) {
            header_.setFlag(ContainerHeader::kFlagCompactable);
        }
        if (kind == ContainerKind::CodeBlock) {
            header_.setFlag(ContainerHeader::kFlagExecutable);
            header_.clearFlag(ContainerHeader::kFlagCompactable);
        }
    }

    const ContainerHeader& header() const { return header_; }
    ContainerHeader& header() { return header_; }

    ContainerKind kind() const { return header_.kind; }
    uintptr_t base() const { return base_; }
    size_t size() const { return size_; }
    uint32_t zoneId() const { return header_.zoneId; }
    uint16_t sizeClass() const { return header_.sizeClass; }
    uint32_t cellCount() const { return header_.cellCount; }
    uint32_t liveCells() const { return header_.liveCells; }

    bool contains(uintptr_t addr) const {
        return addr >= base_ && addr < base_ + size_;
    }

    // Cell index from address.
    int32_t cellIndex(uintptr_t addr) const {
        if (!contains(addr) || header_.sizeClass == 0) return -1;
        size_t offset = addr - base_;
        if (offset % header_.sizeClass != 0) return -1;
        uint32_t idx = static_cast<uint32_t>(offset / header_.sizeClass);
        return idx < header_.cellCount ? static_cast<int32_t>(idx) : -1;
    }

    // Cell address from index.
    uintptr_t cellAddress(uint32_t index) const {
        assert(index < header_.cellCount);
        return base_ + (index * header_.sizeClass);
    }

    void recordAllocation() { header_.liveCells++; }

    void recordFree() {
        if (header_.liveCells > 0) header_.liveCells--;
    }

    void setLiveCells(uint32_t count) { header_.liveCells = count; }

private:
    uintptr_t base_;
    size_t size_;
    ContainerHeader header_;
};

// Container registry: maps address ranges to containers for O(log n) lookup.
class ContainerRegistry {
public:
    void add(CellContainer* container) {
        std::lock_guard<std::mutex> lock(mutex_);
        containers_.push_back(container);
    }

    void remove(CellContainer* container) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = containers_.begin(); it != containers_.end(); ++it) {
            if (*it == container) {
                containers_.erase(it);
                return;
            }
        }
    }

    CellContainer* findContaining(uintptr_t addr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* c : containers_) {
            if (c->contains(addr)) return c;
        }
        return nullptr;
    }

    template<typename Fn>
    void forEach(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* c : containers_) fn(c);
    }

    template<typename Fn>
    void forEachInZone(uint32_t zoneId, Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* c : containers_) {
            if (c->zoneId() == zoneId) fn(c);
        }
    }

    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return containers_.size();
    }

    size_t totalBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (auto* c : containers_) total += c->size();
        return total;
    }

    size_t liveBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (auto* c : containers_) total += c->liveCells() * c->sizeClass();
        return total;
    }

private:
    mutable std::mutex mutex_;
    std::vector<CellContainer*> containers_;
};

} // namespace Zepra::Heap
