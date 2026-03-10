// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_capacity_test.cpp — Tests for GC object capacity at 80K+ and generation balancing

#include <vector>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <functional>
#include <cstring>

namespace Zepra::Test {

// =============================================================================
// Simulated ObjectTable for testing
// =============================================================================

class TestObjectTable {
public:
    struct Entry {
        uintptr_t addr;
        uint32_t size;
        bool marked;
        bool alive() const { return addr != 0; }
    };

    explicit TestObjectTable(size_t capacity) : entries_(capacity), count_(0) {}

    uint32_t insert(uintptr_t addr, uint32_t size) {
        for (size_t i = 0; i < entries_.size(); i++) {
            if (!entries_[i].alive()) {
                entries_[i].addr = addr;
                entries_[i].size = size;
                entries_[i].marked = false;
                count_++;
                return static_cast<uint32_t>(i);
            }
        }
        return UINT32_MAX;
    }

    void remove(uint32_t slot) {
        if (slot < entries_.size() && entries_[slot].alive()) {
            entries_[slot] = {};
            count_--;
        }
    }

    void clearMarks() {
        for (auto& e : entries_) e.marked = false;
    }

    size_t sweep() {
        size_t freed = 0;
        for (auto& e : entries_) {
            if (e.alive() && !e.marked) {
                freed += e.size;
                e = {};
                count_--;
            }
        }
        return freed;
    }

    bool resize(size_t newCapacity) {
        if (newCapacity < count_) return false;
        entries_.resize(newCapacity);
        return true;
    }

    size_t count() const { return count_; }
    size_t capacity() const { return entries_.size(); }
    Entry* get(uint32_t slot) { return slot < entries_.size() && entries_[slot].alive() ? &entries_[slot] : nullptr; }

private:
    std::vector<Entry> entries_;
    size_t count_ = 0;
};

// =============================================================================
// Test Runner
// =============================================================================

struct CapacityTestResult {
    const char* name;
    bool passed;
    const char* failReason;
};

using TestFn = std::function<bool(const char*& fail)>;

class CapacityTestRunner {
public:
    void add(const char* name, TestFn fn) {
        tests_.push_back({name, std::move(fn)});
    }

    size_t runAll() {
        size_t passed = 0, failed = 0;
        fprintf(stderr, "\n=== GC Capacity Tests ===\n\n");

        for (auto& [name, fn] : tests_) {
            const char* fail = nullptr;
            bool ok = false;
            try { ok = fn(fail); } catch (...) {
                fail = "exception"; ok = false;
            }
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
    std::vector<std::pair<const char*, TestFn>> tests_;
};

// =============================================================================
// Tests
// =============================================================================

static void registerCapacityTests(CapacityTestRunner& runner) {

    runner.add("Allocate80KObjects", [](const char*& fail) -> bool {
        TestObjectTable table(80000);

        for (uint32_t i = 0; i < 80000; i++) {
            uintptr_t addr = 0x10000000 + i * 64;
            uint32_t slot = table.insert(addr, 64);
            if (slot == UINT32_MAX) {
                fail = "insertion failed before 80K";
                return false;
            }
        }

        if (table.count() != 80000) {
            fail = "count mismatch at 80K";
            return false;
        }
        return true;
    });

    runner.add("Allocate80KThenSweep", [](const char*& fail) -> bool {
        TestObjectTable table(80000);

        for (uint32_t i = 0; i < 80000; i++) {
            table.insert(0x10000000 + i * 64, 64);
        }

        // Mark half as live.
        for (uint32_t i = 0; i < 80000; i += 2) {
            auto* e = table.get(i);
            if (e) e->marked = true;
        }

        size_t freed = table.sweep();
        if (freed == 0) { fail = "sweep freed nothing"; return false; }
        if (table.count() != 40000) { fail = "expected 40K survivors"; return false; }
        return true;
    });

    runner.add("TableGrowBeyond80K", [](const char*& fail) -> bool {
        TestObjectTable table(80000);

        for (uint32_t i = 0; i < 80000; i++) {
            table.insert(0x10000000 + i * 64, 64);
        }

        // Try to grow to 160K.
        if (!table.resize(160000)) {
            fail = "resize to 160K failed";
            return false;
        }

        // Insert more.
        for (uint32_t i = 0; i < 20000; i++) {
            uint32_t slot = table.insert(0x20000000 + i * 64, 64);
            if (slot == UINT32_MAX) {
                fail = "insertion failed after grow";
                return false;
            }
        }

        if (table.count() != 100000) { fail = "expected 100K objects"; return false; }
        return true;
    });

    runner.add("TableShrink", [](const char*& fail) -> bool {
        TestObjectTable table(80000);

        for (uint32_t i = 0; i < 10000; i++) {
            table.insert(0x10000000 + i * 64, 64);
        }

        if (!table.resize(20000)) { fail = "shrink failed"; return false; }
        if (table.capacity() != 20000) { fail = "capacity wrong after shrink"; return false; }
        if (table.count() != 10000) { fail = "count changed after shrink"; return false; }
        return true;
    });

    runner.add("ShrinkBelowCountFails", [](const char*& fail) -> bool {
        TestObjectTable table(80000);

        for (uint32_t i = 0; i < 50000; i++) {
            table.insert(0x10000000 + i * 64, 64);
        }

        // Should fail — can't shrink below live count.
        if (table.resize(30000)) {
            fail = "resize below count should fail";
            return false;
        }
        return true;
    });

    runner.add("ClearMarksCorrectness", [](const char*& fail) -> bool {
        TestObjectTable table(1000);

        for (uint32_t i = 0; i < 500; i++) {
            table.insert(0x10000000 + i * 64, 64);
        }

        // Mark all.
        for (uint32_t i = 0; i < 500; i++) {
            auto* e = table.get(i);
            if (e) e->marked = true;
        }

        table.clearMarks();

        // Sweep should now free everything.
        table.sweep();
        if (table.count() != 0) { fail = "all should be freed after clear+sweep"; return false; }
        return true;
    });

    runner.add("InsertAfterRemove", [](const char*& fail) -> bool {
        TestObjectTable table(100);

        for (uint32_t i = 0; i < 100; i++) {
            table.insert(0x10000000 + i * 64, 64);
        }

        // Remove slot 50.
        table.remove(50);
        if (table.count() != 99) { fail = "remove count wrong"; return false; }

        // Re-insert should succeed.
        uint32_t slot = table.insert(0xDEAD0000, 128);
        if (slot == UINT32_MAX) { fail = "re-insert failed"; return false; }
        if (table.count() != 100) { fail = "count after re-insert wrong"; return false; }
        return true;
    });

    runner.add("PeakTracking", [](const char*& fail) -> bool {
        TestObjectTable table(80000);
        size_t peak = 0;

        for (uint32_t i = 0; i < 60000; i++) {
            table.insert(0x10000000 + i * 64, 64);
            if (table.count() > peak) peak = table.count();
        }

        // Sweep half.
        for (uint32_t i = 0; i < 60000; i += 2) {
            auto* e = table.get(i);
            if (e) e->marked = true;
        }
        table.sweep();

        if (peak != 60000) { fail = "peak should be 60K"; return false; }
        if (table.count() != 30000) { fail = "post-sweep count wrong"; return false; }
        return true;
    });
}

static size_t runCapacityTests() {
    CapacityTestRunner runner;
    registerCapacityTests(runner);
    return runner.runAll();
}

} // namespace Zepra::Test
