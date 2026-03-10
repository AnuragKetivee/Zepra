// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_alloc_kind.cpp — Size class registry, kind→arena mapping

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <array>
#include <atomic>

namespace Zepra::Heap {

enum class AllocKind : uint8_t {
    Cell16       = 0,
    Cell32       = 1,
    Cell48       = 2,
    Cell64       = 3,
    Cell128      = 4,
    Cell256      = 5,
    Cell512      = 6,
    Cell1024     = 7,
    Cell2048     = 8,
    LargeObject  = 9,
    Count        = 10,
};

struct AllocKindInfo {
    AllocKind kind;
    uint16_t cellSize;
    uint16_t cellsPerArena;    // Based on 16KB arenas
    bool canBgSweep;           // Eligible for background sweeping
    bool canCompact;           // Eligible for compaction
    bool hasDestructor;        // Cells may need destructor call
    const char* name;
};

class AllocKindRegistry {
public:
    static constexpr size_t kKindCount = static_cast<size_t>(AllocKind::Count);
    static constexpr uint16_t kArenaUsable = 16384 - 128;  // Arena size minus header

    AllocKindRegistry() {
        registerKind(AllocKind::Cell16,      16,   true,  true,  false, "Cell16");
        registerKind(AllocKind::Cell32,      32,   true,  true,  false, "Cell32");
        registerKind(AllocKind::Cell48,      48,   true,  true,  false, "Cell48");
        registerKind(AllocKind::Cell64,      64,   true,  true,  true,  "Cell64");
        registerKind(AllocKind::Cell128,     128,  true,  true,  true,  "Cell128");
        registerKind(AllocKind::Cell256,     256,  true,  true,  true,  "Cell256");
        registerKind(AllocKind::Cell512,     512,  true,  false, true,  "Cell512");
        registerKind(AllocKind::Cell1024,    1024, true,  false, true,  "Cell1024");
        registerKind(AllocKind::Cell2048,    2048, false, false, true,  "Cell2048");
        registerKind(AllocKind::LargeObject, 0,    false, false, true,  "LargeObject");

        buildSizeLookup();
    }

    const AllocKindInfo& info(AllocKind kind) const {
        size_t idx = static_cast<size_t>(kind);
        assert(idx < kKindCount);
        return kinds_[idx];
    }

    // Map a requested size to the smallest fitting AllocKind.
    AllocKind kindForSize(size_t size) const {
        if (size <= 16) return AllocKind::Cell16;
        if (size <= 32) return AllocKind::Cell32;
        if (size <= 48) return AllocKind::Cell48;
        if (size <= 64) return AllocKind::Cell64;
        if (size <= 128) return AllocKind::Cell128;
        if (size <= 256) return AllocKind::Cell256;
        if (size <= 512) return AllocKind::Cell512;
        if (size <= 1024) return AllocKind::Cell1024;
        if (size <= 2048) return AllocKind::Cell2048;
        return AllocKind::LargeObject;
    }

    uint16_t cellSize(AllocKind kind) const { return info(kind).cellSize; }
    uint16_t cellsPerArena(AllocKind kind) const { return info(kind).cellsPerArena; }
    bool canBgSweep(AllocKind kind) const { return info(kind).canBgSweep; }
    bool canCompact(AllocKind kind) const { return info(kind).canCompact; }
    bool hasDestructor(AllocKind kind) const { return info(kind).hasDestructor; }
    const char* name(AllocKind kind) const { return info(kind).name; }

    // Size-class waste calculation.
    size_t wasteFor(size_t requestSize) const {
        AllocKind kind = kindForSize(requestSize);
        uint16_t cs = cellSize(kind);
        return cs > requestSize ? cs - requestSize : 0;
    }

    double wasteRatio(size_t requestSize) const {
        AllocKind kind = kindForSize(requestSize);
        uint16_t cs = cellSize(kind);
        return cs > 0 ? static_cast<double>(wasteFor(requestSize)) / cs : 0;
    }

    // Total arena utilization for a given kind.
    double arenaUtilization(AllocKind kind) const {
        uint16_t cs = cellSize(kind);
        uint16_t count = cellsPerArena(kind);
        return cs > 0 && kArenaUsable > 0
            ? static_cast<double>(cs * count) / kArenaUsable
            : 0;
    }

    // Statistics tracking per kind.
    struct KindStats {
        std::atomic<uint64_t> allocations{0};
        std::atomic<uint64_t> frees{0};
        std::atomic<uint64_t> bytesAllocated{0};
    };

    void recordAllocation(AllocKind kind) {
        size_t idx = static_cast<size_t>(kind);
        if (idx < kKindCount) {
            stats_[idx].allocations.fetch_add(1, std::memory_order_relaxed);
            stats_[idx].bytesAllocated.fetch_add(cellSize(kind), std::memory_order_relaxed);
        }
    }

    void recordFree(AllocKind kind) {
        size_t idx = static_cast<size_t>(kind);
        if (idx < kKindCount) {
            stats_[idx].frees.fetch_add(1, std::memory_order_relaxed);
        }
    }

    uint64_t allocations(AllocKind kind) const {
        return stats_[static_cast<size_t>(kind)].allocations.load(std::memory_order_relaxed);
    }

    uint64_t frees(AllocKind kind) const {
        return stats_[static_cast<size_t>(kind)].frees.load(std::memory_order_relaxed);
    }

    uint64_t liveCount(AllocKind kind) const {
        uint64_t a = allocations(kind);
        uint64_t f = frees(kind);
        return a > f ? a - f : 0;
    }

    void resetStats() {
        for (size_t i = 0; i < kKindCount; i++) {
            stats_[i].allocations.store(0, std::memory_order_relaxed);
            stats_[i].frees.store(0, std::memory_order_relaxed);
            stats_[i].bytesAllocated.store(0, std::memory_order_relaxed);
        }
    }

    // Check if a given size is a valid size class boundary.
    bool isExactSizeClass(size_t size) const {
        for (size_t i = 0; i < kKindCount - 1; i++) {
            if (kinds_[i].cellSize == size) return true;
        }
        return false;
    }

    // Iterate over all non-LargeObject kinds.
    template<typename Fn>
    void forEachSmallKind(Fn&& fn) const {
        for (size_t i = 0; i < kKindCount; i++) {
            if (kinds_[i].kind != AllocKind::LargeObject) {
                fn(kinds_[i]);
            }
        }
    }

private:
    void registerKind(AllocKind kind, uint16_t cellSize, bool bgSweep, bool compact,
                      bool destructor, const char* name) {
        size_t idx = static_cast<size_t>(kind);
        kinds_[idx].kind = kind;
        kinds_[idx].cellSize = cellSize;
        kinds_[idx].cellsPerArena = cellSize > 0 ? kArenaUsable / cellSize : 0;
        kinds_[idx].canBgSweep = bgSweep;
        kinds_[idx].canCompact = compact;
        kinds_[idx].hasDestructor = destructor;
        kinds_[idx].name = name;
    }

    void buildSizeLookup() {
        memset(sizeLookup_, 0, sizeof(sizeLookup_));
        for (size_t size = 1; size <= 2048; size++) {
            AllocKind kind = kindForSize(size);
            if (size <= 256) {
                sizeLookup_[size] = static_cast<uint8_t>(kind);
            }
        }
    }

    AllocKindInfo kinds_[kKindCount];
    KindStats stats_[kKindCount];
    uint8_t sizeLookup_[257];  // Fast lookup for sizes ≤ 256
};

} // namespace Zepra::Heap
