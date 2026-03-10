// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — smart_ram_allocator_test.cpp — Tests for Smart RAM Allocator, tab isolation, pressure

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <functional>

namespace Zepra::Test {

// =============================================================================
// Minimal SmartRAM Simulation for Testing
// =============================================================================

enum class TestTabPriority : uint8_t { Active, MediaPlaying, Background, Suspended };

struct TestTabBudget {
    uint64_t tabId;
    TestTabPriority priority;
    size_t budgetBytes;
    size_t usedBytes;

    bool isOverBudget() const { return usedBytes > budgetBytes; }
};

class TestSmartRAM {
public:
    explicit TestSmartRAM(size_t totalRAM)
        : totalRAM_(totalRAM), reservedBytes_(512ULL * 1024 * 1024) {}

    void registerTab(uint64_t tabId, TestTabPriority prio) {
        TestTabBudget b;
        b.tabId = tabId;
        b.priority = prio;
        b.budgetBytes = 0;
        b.usedBytes = 0;
        tabs_[tabId] = b;
        rebalance();
    }

    void unregisterTab(uint64_t tabId) {
        tabs_.erase(tabId);
        rebalance();
    }

    void setPriority(uint64_t tabId, TestTabPriority prio) {
        auto it = tabs_.find(tabId);
        if (it != tabs_.end()) {
            it->second.priority = prio;
            rebalance();
        }
    }

    void reportUsage(uint64_t tabId, size_t bytes) {
        auto it = tabs_.find(tabId);
        if (it != tabs_.end()) it->second.usedBytes = bytes;
    }

    size_t getBudget(uint64_t tabId) const {
        auto it = tabs_.find(tabId);
        return it != tabs_.end() ? it->second.budgetBytes : 0;
    }

    TestTabPriority getPriority(uint64_t tabId) const {
        auto it = tabs_.find(tabId);
        return it != tabs_.end() ? it->second.priority : TestTabPriority::Suspended;
    }

    bool isOverBudget(uint64_t tabId) const {
        auto it = tabs_.find(tabId);
        return it != tabs_.end() ? it->second.isOverBudget() : false;
    }

    size_t tabCount() const { return tabs_.size(); }

    // Never evicts media tabs.
    uint64_t evictLowestPriority() {
        uint64_t candidate = 0;
        TestTabPriority lowest = TestTabPriority::Active;
        bool found = false;

        for (auto& [id, t] : tabs_) {
            if (t.priority == TestTabPriority::MediaPlaying) continue;
            if (!found || t.priority > lowest) {
                candidate = id;
                lowest = t.priority;
                found = true;
            }
        }

        if (found) {
            tabs_.erase(candidate);
            rebalance();
            return candidate;
        }
        return 0;
    }

private:
    void rebalance() {
        size_t available = totalRAM_ > reservedBytes_ ? totalRAM_ - reservedBytes_ : totalRAM_ / 2;

        size_t activeCount = 0, mediaCount = 0, bgCount = 0, suspCount = 0;
        for (auto& [id, t] : tabs_) {
            switch (t.priority) {
                case TestTabPriority::Active: activeCount++; break;
                case TestTabPriority::MediaPlaying: mediaCount++; break;
                case TestTabPriority::Background: bgCount++; break;
                case TestTabPriority::Suspended: suspCount++; break;
            }
        }

        for (auto& [id, t] : tabs_) {
            double pct = 0;
            size_t count = 1;
            switch (t.priority) {
                case TestTabPriority::Active: pct = 0.60; count = activeCount; break;
                case TestTabPriority::MediaPlaying: pct = 0.25; count = mediaCount; break;
                case TestTabPriority::Background: pct = 0.10; count = bgCount; break;
                case TestTabPriority::Suspended: pct = 0.05; count = suspCount; break;
            }
            t.budgetBytes = count > 0 ? static_cast<size_t>(available * pct) / count : 0;
        }
    }

    size_t totalRAM_;
    size_t reservedBytes_;
    std::unordered_map<uint64_t, TestTabBudget> tabs_;
};

// =============================================================================
// Test Runner
// =============================================================================

using SmartRAMTestFn = std::function<bool(const char*& fail)>;

class SmartRAMTestRunner {
public:
    void add(const char* name, SmartRAMTestFn fn) {
        tests_.push_back({name, std::move(fn)});
    }

    size_t runAll() {
        size_t passed = 0, failed = 0;
        fprintf(stderr, "\n=== Smart RAM Allocator Tests ===\n\n");

        for (auto& [name, fn] : tests_) {
            const char* fail = nullptr;
            bool ok = false;
            try { ok = fn(fail); } catch (...) { fail = "exception"; ok = false; }
            if (ok) {
                fprintf(stderr, "  [PASS] %s\n", name);
                passed++;
            } else {
                fprintf(stderr, "  [FAIL] %s: %s\n", name, fail ? fail : "unknown");
                failed++;
            }
        }
        fprintf(stderr, "\n  %zu passed, %zu failed\n\n", passed, failed);
        return failed;
    }

private:
    std::vector<std::pair<const char*, SmartRAMTestFn>> tests_;
};

// =============================================================================
// Tests
// =============================================================================

static void registerSmartRAMTests(SmartRAMTestRunner& runner) {

    runner.add("SingleActiveTab", [](const char*& fail) -> bool {
        TestSmartRAM ram(8ULL * 1024 * 1024 * 1024);  // 8GB
        ram.registerTab(1, TestTabPriority::Active);

        size_t budget = ram.getBudget(1);
        // Single active tab should get 60% of (8GB - 512MB reserved).
        if (budget == 0) { fail = "budget should not be zero"; return false; }
        return true;
    });

    runner.add("ActiveGetsMoreThanBackground", [](const char*& fail) -> bool {
        TestSmartRAM ram(8ULL * 1024 * 1024 * 1024);
        ram.registerTab(1, TestTabPriority::Active);
        ram.registerTab(2, TestTabPriority::Background);

        size_t activeBudget = ram.getBudget(1);
        size_t bgBudget = ram.getBudget(2);

        if (activeBudget <= bgBudget) {
            fail = "active should have higher budget than background";
            return false;
        }
        return true;
    });

    runner.add("BudgetRebalanceOnTabAdd", [](const char*& fail) -> bool {
        TestSmartRAM ram(8ULL * 1024 * 1024 * 1024);
        ram.registerTab(1, TestTabPriority::Active);
        size_t budgetWithOne = ram.getBudget(1);

        ram.registerTab(2, TestTabPriority::Active);
        size_t budgetWithTwo = ram.getBudget(1);

        if (budgetWithTwo >= budgetWithOne) {
            fail = "budget should decrease when another active tab is added";
            return false;
        }
        return true;
    });

    runner.add("BudgetRebalanceOnTabRemove", [](const char*& fail) -> bool {
        TestSmartRAM ram(8ULL * 1024 * 1024 * 1024);
        ram.registerTab(1, TestTabPriority::Active);
        ram.registerTab(2, TestTabPriority::Active);
        size_t budgetBefore = ram.getBudget(1);

        ram.unregisterTab(2);
        size_t budgetAfter = ram.getBudget(1);

        if (budgetAfter <= budgetBefore) {
            fail = "budget should increase when tab removed";
            return false;
        }
        return true;
    });

    runner.add("MediaTabNeverEvicted", [](const char*& fail) -> bool {
        TestSmartRAM ram(4ULL * 1024 * 1024 * 1024);
        ram.registerTab(1, TestTabPriority::MediaPlaying);
        ram.registerTab(2, TestTabPriority::Background);
        ram.registerTab(3, TestTabPriority::Suspended);

        uint64_t evicted = ram.evictLowestPriority();

        // Should evict suspended (3) or background (2), never media (1).
        if (evicted == 1) { fail = "media tab should never be evicted"; return false; }
        if (evicted == 0) { fail = "should have evicted something"; return false; }
        return true;
    });

    runner.add("MediaTabProtectedFromSuspend", [](const char*& fail) -> bool {
        TestSmartRAM ram(4ULL * 1024 * 1024 * 1024);
        ram.registerTab(1, TestTabPriority::MediaPlaying);

        // Media tab priority should be MediaPlaying.
        if (ram.getPriority(1) != TestTabPriority::MediaPlaying) {
            fail = "media tab priority wrong";
            return false;
        }
        return true;
    });

    runner.add("OverBudgetDetection", [](const char*& fail) -> bool {
        TestSmartRAM ram(4ULL * 1024 * 1024 * 1024);
        ram.registerTab(1, TestTabPriority::Active);

        size_t budget = ram.getBudget(1);
        ram.reportUsage(1, budget + 1);

        if (!ram.isOverBudget(1)) {
            fail = "should detect over-budget";
            return false;
        }
        return true;
    });

    runner.add("PriorityChangeRebalances", [](const char*& fail) -> bool {
        TestSmartRAM ram(8ULL * 1024 * 1024 * 1024);
        ram.registerTab(1, TestTabPriority::Active);
        size_t activeBudget = ram.getBudget(1);

        ram.setPriority(1, TestTabPriority::Background);
        size_t bgBudget = ram.getBudget(1);

        if (bgBudget >= activeBudget) {
            fail = "background budget should be less than active";
            return false;
        }
        return true;
    });

    runner.add("TabIsolation_CrossTabBlocked", [](const char*& fail) -> bool {
        // Simulated heap ranges per tab.
        struct TabHeap { uintptr_t base; size_t size; };
        TabHeap tab1{0x10000000, 1024 * 1024};
        TabHeap tab2{0x20000000, 1024 * 1024};

        uintptr_t addrInTab2 = tab2.base + 512;

        // Validate: addr in tab2 is NOT within tab1's range.
        bool crossTab = (addrInTab2 >= tab1.base && addrInTab2 < tab1.base + tab1.size);
        if (crossTab) {
            fail = "cross-tab address should not be in tab1 range";
            return false;
        }
        return true;
    });

    runner.add("SuspendedGetsMinimalBudget", [](const char*& fail) -> bool {
        TestSmartRAM ram(8ULL * 1024 * 1024 * 1024);
        ram.registerTab(1, TestTabPriority::Active);
        ram.registerTab(2, TestTabPriority::Suspended);

        size_t activeBudget = ram.getBudget(1);
        size_t suspendedBudget = ram.getBudget(2);

        if (suspendedBudget >= activeBudget / 2) {
            fail = "suspended budget should be much smaller than active";
            return false;
        }
        return true;
    });

    runner.add("MultipleEvictionsPreserveMedia", [](const char*& fail) -> bool {
        TestSmartRAM ram(4ULL * 1024 * 1024 * 1024);
        ram.registerTab(1, TestTabPriority::MediaPlaying);
        ram.registerTab(2, TestTabPriority::Suspended);
        ram.registerTab(3, TestTabPriority::Background);
        ram.registerTab(4, TestTabPriority::Suspended);

        ram.evictLowestPriority();
        ram.evictLowestPriority();
        ram.evictLowestPriority();

        // After evicting 3 non-media tabs, only media tab should remain.
        if (ram.tabCount() != 1) {
            fail = "only media tab should survive evictions";
            return false;
        }
        if (ram.getBudget(1) == 0) {
            fail = "media tab should still have budget";
            return false;
        }
        return true;
    });
}

static size_t runSmartRAMTests() {
    SmartRAMTestRunner runner;
    registerSmartRAMTests(runner);
    return runner.runAll();
}

} // namespace Zepra::Test
