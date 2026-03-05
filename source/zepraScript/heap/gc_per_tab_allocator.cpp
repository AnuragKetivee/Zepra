// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_per_tab_allocator.cpp — Isolated allocator per tab context

#include <mutex>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <cstdio>
#include <cassert>

namespace Zepra::Heap {

// Each tab gets its own bump-pointer allocator within its isolated
// heap region. This ensures one tab cannot exhaust another's memory.

class PerTabAllocator {
public:
    struct TabAllocState {
        uintptr_t base;
        uintptr_t cursor;
        uintptr_t limit;
        size_t totalAllocated;
        size_t maxBytes;

        TabAllocState()
            : base(0), cursor(0), limit(0)
            , totalAllocated(0), maxBytes(0) {}

        size_t remaining() const { return limit > cursor ? limit - cursor : 0; }
        size_t used() const { return cursor - base; }
    };

    // Initialize allocator for a tab.
    void initTab(uint64_t tabId, uintptr_t base, size_t size, size_t maxBytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        TabAllocState state;
        state.base = base;
        state.cursor = base;
        state.limit = base + size;
        state.maxBytes = maxBytes;
        tabs_[tabId] = state;
    }

    // Bump-pointer allocation within a tab's region.
    uintptr_t allocate(uint64_t tabId, size_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tabs_.find(tabId);
        if (it == tabs_.end()) return 0;

        auto& state = it->second;

        // Check tab memory limit.
        if (state.totalAllocated + bytes > state.maxBytes) return 0;

        // Align to 8 bytes.
        bytes = (bytes + 7) & ~static_cast<size_t>(7);

        if (state.cursor + bytes > state.limit) return 0;

        uintptr_t addr = state.cursor;
        state.cursor += bytes;
        state.totalAllocated += bytes;

        return addr;
    }

    // Reset a tab's allocator after scavenge.
    void resetTab(uint64_t tabId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tabs_.find(tabId);
        if (it == tabs_.end()) return;
        it->second.cursor = it->second.base;
    }

    // Remove a tab's allocator.
    void removeTab(uint64_t tabId) {
        std::lock_guard<std::mutex> lock(mutex_);
        tabs_.erase(tabId);
    }

    // Per-tab memory usage (for browser task manager).
    size_t tabUsed(uint64_t tabId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tabs_.find(tabId);
        return it != tabs_.end() ? it->second.used() : 0;
    }

    size_t tabRemaining(uint64_t tabId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tabs_.find(tabId);
        return it != tabs_.end() ? it->second.remaining() : 0;
    }

    size_t tabTotal(uint64_t tabId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tabs_.find(tabId);
        return it != tabs_.end() ? it->second.totalAllocated : 0;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, TabAllocState> tabs_;
};

} // namespace Zepra::Heap
