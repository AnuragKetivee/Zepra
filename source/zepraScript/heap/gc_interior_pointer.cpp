// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_interior_pointer.cpp — Interior pointer lookup: find cell from any address

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <vector>
#include <algorithm>
#include <mutex>

namespace Zepra::Heap {

struct PageEntry {
    uintptr_t base;
    size_t size;
    uint16_t cellSize;     // Cell size for this page
    uint32_t cellCount;
    uint32_t zoneId;

    uintptr_t end() const { return base + size; }

    bool contains(uintptr_t addr) const {
        return addr >= base && addr < end();
    }

    // Find the cell that contains the given interior pointer.
    uintptr_t cellStartFor(uintptr_t addr) const {
        if (!contains(addr) || cellSize == 0) return 0;
        size_t offset = addr - base;
        size_t cellOffset = (offset / cellSize) * cellSize;
        return base + cellOffset;
    }

    uint32_t cellIndexFor(uintptr_t addr) const {
        if (!contains(addr) || cellSize == 0) return UINT32_MAX;
        size_t offset = addr - base;
        return static_cast<uint32_t>(offset / cellSize);
    }
};

class InteriorPointerMap {
public:
    void registerPage(uintptr_t base, size_t size, uint16_t cellSize,
                      uint32_t cellCount, uint32_t zoneId) {
        std::lock_guard<std::mutex> lock(mutex_);
        pages_.push_back({base, size, cellSize, cellCount, zoneId});
        sorted_ = false;
    }

    void unregisterPage(uintptr_t base) {
        std::lock_guard<std::mutex> lock(mutex_);
        pages_.erase(
            std::remove_if(pages_.begin(), pages_.end(),
                [base](const PageEntry& e) { return e.base == base; }),
            pages_.end());
    }

    // Find the cell start address for any interior pointer.
    // Returns 0 if the pointer is not in any registered page.
    uintptr_t findCell(uintptr_t interiorPtr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureSorted();

        // Binary search for containing page.
        const PageEntry* page = findPage(interiorPtr);
        if (!page) return 0;

        return page->cellStartFor(interiorPtr);
    }

    // Find cell with full metadata.
    struct CellInfo {
        uintptr_t cellStart;
        uint16_t cellSize;
        uint32_t cellIndex;
        uint32_t zoneId;
        bool found;

        CellInfo() : cellStart(0), cellSize(0), cellIndex(UINT32_MAX)
            , zoneId(0), found(false) {}
    };

    CellInfo findCellInfo(uintptr_t interiorPtr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureSorted();

        CellInfo info;
        const PageEntry* page = findPage(interiorPtr);
        if (!page) return info;

        info.cellStart = page->cellStartFor(interiorPtr);
        info.cellSize = page->cellSize;
        info.cellIndex = page->cellIndexFor(interiorPtr);
        info.zoneId = page->zoneId;
        info.found = info.cellStart != 0;
        return info;
    }

    // Check if address is in any registered page.
    bool isInHeap(uintptr_t addr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureSorted();
        return findPage(addr) != nullptr;
    }

    // Check if address is at a cell boundary (not interior).
    bool isCellBoundary(uintptr_t addr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureSorted();

        const PageEntry* page = findPage(addr);
        if (!page || page->cellSize == 0) return false;

        size_t offset = addr - page->base;
        return (offset % page->cellSize) == 0;
    }

    size_t pageCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pages_.size();
    }

    size_t totalMappedBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (auto& p : pages_) total += p.size;
        return total;
    }

private:
    const PageEntry* findPage(uintptr_t addr) const {
        // Binary search: find the last page with base <= addr.
        int lo = 0;
        int hi = static_cast<int>(pages_.size()) - 1;
        const PageEntry* candidate = nullptr;

        while (lo <= hi) {
            int mid = lo + (hi - lo) / 2;
            if (pages_[mid].base <= addr) {
                candidate = &pages_[mid];
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }

        if (candidate && candidate->contains(addr)) return candidate;
        return nullptr;
    }

    void ensureSorted() const {
        if (!sorted_) {
            auto& mutablePages = const_cast<std::vector<PageEntry>&>(pages_);
            std::sort(mutablePages.begin(), mutablePages.end(),
                [](const PageEntry& a, const PageEntry& b) { return a.base < b.base; });
            const_cast<bool&>(sorted_) = true;
        }
    }

    mutable std::mutex mutex_;
    std::vector<PageEntry> pages_;
    bool sorted_ = false;
};

} // namespace Zepra::Heap
