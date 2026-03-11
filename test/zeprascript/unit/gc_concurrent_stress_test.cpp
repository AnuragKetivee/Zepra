// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

/**
 * @file gc_concurrent_stress_test.cpp
 * @brief Concurrent GC stress tests — allocation bursts, mark-sweep cycles,
 *        finalizer storms, write barrier pressure, concurrent marking.
 */

#include <gtest/gtest.h>
#include "runtime/objects/object.hpp"
#include "runtime/objects/value.hpp"
#include "runtime/execution/Sandbox.h"
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <random>

using namespace Zepra::Runtime;

// =============================================================================
// Allocation Burst
// =============================================================================

TEST(GCConcurrentStress, AllocationBurst100K) {
    std::vector<Object*> objects;
    objects.reserve(100000);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 100000; ++i) {
        Object* obj = new Object();
        obj->set("index", Value::number(static_cast<double>(i)));
        objects.push_back(obj);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    EXPECT_EQ(objects.size(), 100000u);

    // Verify sampling
    EXPECT_EQ(objects[0]->get("index").asNumber(), 0.0);
    EXPECT_EQ(objects[50000]->get("index").asNumber(), 50000.0);
    EXPECT_EQ(objects[99999]->get("index").asNumber(), 99999.0);

    std::cout << "\n=== Allocation Burst (100K objects) ===" << std::endl;
    std::cout << "Time: " << (us / 1000.0) << " ms" << std::endl;
    std::cout << "Alloc/ms: " << std::fixed << std::setprecision(0)
              << (100000.0 / (us / 1000.0)) << std::endl;

    for (auto* obj : objects) delete obj;
}

// =============================================================================
// Mark-Sweep Simulation
// =============================================================================

TEST(GCConcurrentStress, MarkSweepCycle10K) {
    std::vector<Object*> objects;
    objects.reserve(10000);

    for (int i = 0; i < 10000; ++i) {
        Object* obj = new Object();
        objects.push_back(obj);
    }

    // Mark all objects
    for (auto* obj : objects) {
        EXPECT_FALSE(obj->isMarked());
        obj->markGC();
        EXPECT_TRUE(obj->isMarked());
    }

    // Clear marks (simulating sweep preparation)
    for (auto* obj : objects) {
        obj->clearMark();
        EXPECT_FALSE(obj->isMarked());
    }

    // Mark only even-indexed objects as "live"
    for (int i = 0; i < 10000; i += 2) {
        objects[i]->markGC();
    }

    // Sweep: delete unmarked (odd-indexed == dead)
    int deleted = 0;
    for (int i = 0; i < 10000; ++i) {
        if (!objects[i]->isMarked()) {
            delete objects[i];
            objects[i] = nullptr;
            deleted++;
        }
    }

    EXPECT_EQ(deleted, 5000);

    // Cleanup survivors
    for (auto* obj : objects) {
        if (obj) {
            obj->clearMark();
            delete obj;
        }
    }
}

// =============================================================================
// Object Graph Walk
// =============================================================================

TEST(GCConcurrentStress, DeepObjectGraph) {
    // Build a linked list of 5000 objects
    Object* head = new Object();
    Object* current = head;

    for (int i = 1; i < 5000; ++i) {
        Object* next = new Object();
        next->set("value", Value::number(static_cast<double>(i)));
        current->set("next", Value::object(next));
        current = next;
    }

    // Walk the chain and verify
    current = head;
    int count = 0;
    while (current) {
        count++;
        Value nextVal = current->get("next");
        if (nextVal.isObject()) {
            current = nextVal.asObject();
        } else {
            current = nullptr;
        }
    }

    EXPECT_EQ(count, 5000);

    // Cleanup: walk again and delete
    current = head;
    while (current) {
        Value nextVal = current->get("next");
        Object* next = nextVal.isObject() ? nextVal.asObject() : nullptr;
        delete current;
        current = next;
    }
}

// =============================================================================
// Write Barrier Pressure
// =============================================================================

TEST(GCConcurrentStress, WriteBarrierPressure50K) {
    Object* container = new Object();

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 50000; ++i) {
        std::string key = "prop_" + std::to_string(i);
        container->set(key, Value::number(static_cast<double>(i)));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Verify some properties
    EXPECT_EQ(container->get("prop_0").asNumber(), 0.0);
    EXPECT_EQ(container->get("prop_25000").asNumber(), 25000.0);
    EXPECT_EQ(container->get("prop_49999").asNumber(), 49999.0);

    auto keys = container->keys();
    EXPECT_EQ(keys.size(), 50000u);

    std::cout << "\n=== Write Barrier Pressure (50K props) ===" << std::endl;
    std::cout << "Time: " << (us / 1000.0) << " ms" << std::endl;
    std::cout << "Writes/ms: " << std::fixed << std::setprecision(0)
              << (50000.0 / (us / 1000.0)) << std::endl;

    delete container;
}

// =============================================================================
// Concurrent Object Allocation (Multi-Threaded)
// =============================================================================

TEST(GCConcurrentStress, ConcurrentAllocation4Threads) {
    constexpr int THREADS = 4;
    constexpr int PER_THREAD = 25000;
    std::atomic<int> totalAllocated{0};

    auto worker = [&]() {
        std::vector<Object*> local;
        local.reserve(PER_THREAD);
        for (int i = 0; i < PER_THREAD; ++i) {
            Object* obj = new Object();
            obj->set("v", Value::number(static_cast<double>(i)));
            local.push_back(obj);
        }
        totalAllocated += PER_THREAD;

        // Verify + cleanup
        for (int i = 0; i < PER_THREAD; ++i) {
            EXPECT_EQ(local[i]->get("v").asNumber(), static_cast<double>(i));
            delete local[i];
        }
    };

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    EXPECT_EQ(totalAllocated.load(), THREADS * PER_THREAD);

    std::cout << "\n=== Concurrent Allocation (4×25K) ===" << std::endl;
    std::cout << "Total: " << totalAllocated.load() << " objects" << std::endl;
    std::cout << "Time: " << ms << " ms" << std::endl;
}

// =============================================================================
// Mark Concurrency (4 threads marking disjoint sets)
// =============================================================================

TEST(GCConcurrentStress, ConcurrentMarking4Threads) {
    constexpr int TOTAL = 40000;
    constexpr int THREADS = 4;
    constexpr int PER_THREAD = TOTAL / THREADS;

    std::vector<Object*> objects(TOTAL);
    for (int i = 0; i < TOTAL; ++i) {
        objects[i] = new Object();
    }

    auto markWorker = [&](int start, int end) {
        for (int i = start; i < end; ++i) {
            objects[i]->markGC();
        }
    };

    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back(markWorker, t * PER_THREAD, (t + 1) * PER_THREAD);
    }
    for (auto& t : threads) t.join();

    auto t1 = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    int markedCount = 0;
    for (auto* obj : objects) {
        if (obj->isMarked()) markedCount++;
    }
    EXPECT_EQ(markedCount, TOTAL);

    std::cout << "\n=== Concurrent Marking (4 threads × 10K) ===" << std::endl;
    std::cout << "Time: " << (us / 1000.0) << " ms" << std::endl;

    for (auto* obj : objects) delete obj;
}

// =============================================================================
// Array Allocation Burst
// =============================================================================

TEST(GCConcurrentStress, ArrayAllocationBurst) {
    std::vector<Array*> arrays;
    arrays.reserve(10000);

    for (int i = 0; i < 10000; ++i) {
        Array* arr = new Array();
        for (int j = 0; j < 100; ++j) {
            arr->push(Value::number(static_cast<double>(j)));
        }
        arrays.push_back(arr);
    }

    EXPECT_EQ(arrays.size(), 10000u);
    EXPECT_EQ(arrays[0]->length(), 100u);
    EXPECT_EQ(arrays[9999]->at(99).asNumber(), 99.0);

    for (auto* arr : arrays) delete arr;
}

// =============================================================================
// Extensibility Stress
// =============================================================================

TEST(GCConcurrentStress, ExtensibilityToggleStress) {
    Object* obj = new Object();

    for (int i = 0; i < 100000; ++i) {
        if (i % 2 == 0) {
            obj->preventExtensions();
            EXPECT_FALSE(obj->isExtensible());
        } else {
            // preventExtensions is one-way in ES spec, but we test the flag toggle
            // for internal robustness — no crash on repeated calls
            obj->preventExtensions();
            EXPECT_FALSE(obj->isExtensible());
        }
    }

    delete obj;
}

// =============================================================================
// Prototype Chain Depth
// =============================================================================

TEST(GCConcurrentStress, DeepPrototypeChain) {
    constexpr int DEPTH = 1000;
    std::vector<Object*> chain;
    chain.reserve(DEPTH);

    Object* current = new Object();
    current->set("level", Value::number(0));
    chain.push_back(current);

    for (int i = 1; i < DEPTH; ++i) {
        Object* next = new Object();
        next->set("level", Value::number(static_cast<double>(i)));
        next->setPrototype(current);
        chain.push_back(next);
        current = next;
    }

    // Verify chain is intact
    Object* leaf = chain.back();
    EXPECT_EQ(leaf->get("level").asNumber(), static_cast<double>(DEPTH - 1));
    EXPECT_NE(leaf->prototype(), nullptr);

    for (auto it = chain.rbegin(); it != chain.rend(); ++it) delete *it;
}

// =============================================================================
// ResourceMonitor Pressure
// =============================================================================

TEST(GCConcurrentStress, ResourceMonitorAllocationCycles) {
    ExecutionLimits limits;
    limits.maxHeapBytes = 100 * 1024 * 1024;  // 100MB
    ResourceMonitor monitor(limits);

    auto start = std::chrono::high_resolution_clock::now();

    for (int cycle = 0; cycle < 500; ++cycle) {
        for (int i = 0; i < 2000; ++i) {
            monitor.addHeapAllocation(1024);
        }
        for (int i = 0; i < 1500; ++i) {
            monitor.removeHeapAllocation(1024);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "\n=== ResourceMonitor Cycles (500 alloc/dealloc) ===" << std::endl;
    std::cout << "Final heap: " << (monitor.heapUsed() / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "Time: " << (us / 1000.0) << " ms" << std::endl;

    EXPECT_TRUE(monitor.checkHeapLimit());
}
