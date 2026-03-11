// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

/**
 * @file jit_stress_test.cpp
 * @brief JIT profiler and inline cache stress tests — hot detection,
 *        IC hit/miss/polymorphic/megamorphic, profiler reset, threading.
 */

#include <gtest/gtest.h>
#include "jit/jit_profiler.hpp"
#include "runtime/handles/inline_cache.hpp"
#include "runtime/objects/object.hpp"
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>

using namespace Zepra::JIT;
using namespace Zepra::Runtime;

// =============================================================================
// Hot Function Detection
// =============================================================================

TEST(JITStress, HotThreshold) {
    JITProfiler profiler;
    uintptr_t fnId = 0x1000;

    for (uint32_t i = 0; i < FunctionProfile::HOT_THRESHOLD - 1; ++i) {
        profiler.recordCall(fnId);
    }

    auto* profile = profiler.getProfile(fnId);
    ASSERT_NE(profile, nullptr);
    EXPECT_FALSE(profile->isHot());

    profiler.recordCall(fnId);
    EXPECT_TRUE(profile->isHot());
    EXPECT_FALSE(profile->isVeryHot());
}

TEST(JITStress, VeryHotThreshold) {
    JITProfiler profiler;
    uintptr_t fnId = 0x2000;

    for (uint32_t i = 0; i < FunctionProfile::VERY_HOT_THRESHOLD; ++i) {
        profiler.recordCall(fnId);
    }

    auto* profile = profiler.getProfile(fnId);
    ASSERT_NE(profile, nullptr);
    EXPECT_TRUE(profile->isHot());
    EXPECT_TRUE(profile->isVeryHot());
}

// =============================================================================
// Profiler Capacity
// =============================================================================

TEST(JITStress, ProfilerCapacity) {
    JITProfiler profiler;

    for (size_t i = 0; i < JITProfiler::MAX_FUNCTIONS; ++i) {
        profiler.recordCall(static_cast<uintptr_t>(i + 1));
    }

    EXPECT_EQ(profiler.trackedFunctions(), JITProfiler::MAX_FUNCTIONS);
}

TEST(JITStress, ProfilerReset) {
    JITProfiler profiler;
    uintptr_t fnId = 0x3000;

    for (int i = 0; i < 200; ++i) {
        profiler.recordCall(fnId);
    }
    EXPECT_TRUE(profiler.getProfile(fnId)->isHot());

    profiler.reset();
    EXPECT_EQ(profiler.trackedFunctions(), 0u);
    EXPECT_EQ(profiler.getProfile(fnId), nullptr);
}

TEST(JITStress, ProfilerEnable) {
    JITProfiler profiler;
    EXPECT_TRUE(profiler.isEnabled());

    profiler.setEnabled(false);
    EXPECT_FALSE(profiler.isEnabled());

    // Calls while disabled should not record (implementation-dependent)
    profiler.recordCall(0x4000);
    // Re-enable
    profiler.setEnabled(true);
    EXPECT_TRUE(profiler.isEnabled());
}

// =============================================================================
// Hot Function List
// =============================================================================

TEST(JITStress, HotFunctionList) {
    JITProfiler profiler;

    // Make 5 functions hot
    for (int f = 0; f < 5; ++f) {
        uintptr_t id = static_cast<uintptr_t>(0x5000 + f);
        for (uint32_t i = 0; i < FunctionProfile::HOT_THRESHOLD; ++i) {
            profiler.recordCall(id);
        }
    }

    // Add 5 cold functions
    for (int f = 0; f < 5; ++f) {
        profiler.recordCall(static_cast<uintptr_t>(0x6000 + f));
    }

    auto hotList = profiler.getHotFunctions();
    EXPECT_EQ(hotList.size(), 5u);
    EXPECT_EQ(profiler.hotFunctionCount(), 5u);
}

// =============================================================================
// Loop Iteration Recording
// =============================================================================

TEST(JITStress, LoopIterationTracking) {
    JITProfiler profiler;
    uintptr_t fnId = 0x7000;

    profiler.recordCall(fnId);
    for (int i = 0; i < 10000; ++i) {
        profiler.recordLoopIteration(fnId);
    }

    auto* profile = profiler.getProfile(fnId);
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->loopIterations, 10000u);
}

// =============================================================================
// Inline Cache Tests
// =============================================================================

TEST(JITStress, ICMonomorphicHit) {
    InlineCache ic;
    uint32_t shapeId = 42;
    uint32_t offset = 7;

    ic.update(shapeId, offset);

    uint32_t result = 0;
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(ic.lookup(shapeId, result));
        EXPECT_EQ(result, offset);
    }

    EXPECT_GT(ic.hits(), 0u);
}

TEST(JITStress, ICMiss) {
    InlineCache ic;
    ic.update(42, 7);

    uint32_t result = 0;
    EXPECT_FALSE(ic.lookup(99, result));  // Different shape → miss
    EXPECT_GT(ic.misses(), 0u);
}

TEST(JITStress, ICPolymorphic) {
    InlineCache ic;

    // Fill all 4 entries with different shapes
    for (uint32_t i = 0; i < InlineCache::MAX_ENTRIES; ++i) {
        ic.update(100 + i, i * 10);
    }

    // All should hit
    uint32_t result = 0;
    for (uint32_t i = 0; i < InlineCache::MAX_ENTRIES; ++i) {
        EXPECT_TRUE(ic.lookup(100 + i, result));
        EXPECT_EQ(result, i * 10);
    }
}

TEST(JITStress, ICMegamorphicEviction) {
    InlineCache ic;

    // Insert more shapes than MAX_ENTRIES → forces eviction
    for (uint32_t i = 0; i < InlineCache::MAX_ENTRIES + 4; ++i) {
        ic.update(200 + i, i);
    }

    // Some early entries may have been evicted — no crash is the test
    uint32_t result = 0;
    int hits = 0;
    for (uint32_t i = 0; i < InlineCache::MAX_ENTRIES + 4; ++i) {
        if (ic.lookup(200 + i, result)) hits++;
    }

    // At most MAX_ENTRIES should be cached
    EXPECT_LE(static_cast<size_t>(hits), InlineCache::MAX_ENTRIES);
}

TEST(JITStress, ICClear) {
    InlineCache ic;
    ic.update(42, 7);

    uint32_t result = 0;
    EXPECT_TRUE(ic.lookup(42, result));

    ic.clear();
    EXPECT_FALSE(ic.lookup(42, result));
}

TEST(JITStress, ICHitRate) {
    InlineCache ic;
    ic.update(42, 7);

    uint32_t result = 0;
    for (int i = 0; i < 100; ++i) {
        ic.lookup(42, result);  // hit
    }
    for (int i = 0; i < 50; ++i) {
        ic.lookup(99, result);  // miss
    }

    float rate = ic.hitRate();
    EXPECT_GT(rate, 0.6f);
    EXPECT_LT(rate, 0.7f);
}

// =============================================================================
// IC Manager Tests
// =============================================================================

TEST(JITStress, ICManagerGetOrCreate) {
    ICManager manager;

    InlineCache* ic1 = manager.getIC(100);
    InlineCache* ic2 = manager.getIC(200);
    InlineCache* ic1_again = manager.getIC(100);

    EXPECT_NE(ic1, nullptr);
    EXPECT_NE(ic2, nullptr);
    EXPECT_EQ(ic1, ic1_again);  // Same bytecode offset → same IC
}

TEST(JITStress, ICManagerInvalidateAll) {
    ICManager manager;

    InlineCache* ic = manager.getIC(100);
    ic->update(42, 7);

    uint32_t result = 0;
    EXPECT_TRUE(ic->lookup(42, result));

    manager.invalidateAll();

    InlineCache* ic_after = manager.getIC(100);
    EXPECT_FALSE(ic_after->lookup(42, result));
}

TEST(JITStress, ICManagerMemoryUsage) {
    ICManager manager;

    for (size_t i = 0; i < 32; ++i) {
        manager.getIC(i * 10);
    }

    size_t mem = manager.memoryUsage();
    EXPECT_GT(mem, 0u);

    std::cout << "\n=== ICManager Memory ===" << std::endl;
    std::cout << "32 IC sites: " << mem << " bytes" << std::endl;
}

// =============================================================================
// Profiler Throughput
// =============================================================================

TEST(JITStress, RecordCallThroughput) {
    JITProfiler profiler;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000000; ++i) {
        profiler.recordCall(static_cast<uintptr_t>(i % 100 + 1));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    double opsPerSec = (1000000.0 / us) * 1e6;

    std::cout << "\n=== JITProfiler recordCall Throughput (1M) ===" << std::endl;
    std::cout << "Time: " << (us / 1000.0) << " ms" << std::endl;
    std::cout << "Throughput: " << std::fixed << std::setprecision(0)
              << (opsPerSec / 1e6) << " M ops/sec" << std::endl;

    EXPECT_GT(opsPerSec, 1e6);
}
