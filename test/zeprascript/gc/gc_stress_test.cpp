// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <thread>

#include "heap/gc_heap.hpp"
#include "heap/GCController.h"
#include "heap/GCOrchestrator.h"
#include "heap/GCVerifier.h"
#include "heap/gc_cell.h"

namespace Zepra {
namespace heap {
namespace testing {

class GCStressTest : public ::testing::Test {
protected:
    void SetUp() override {
        heap_ = std::make_unique<GCHeap>(256 * 1024 * 1024); // 256MB initial heap
        controller_ = std::make_unique<GCController>(heap_.get());
        orchestrator_ = std::make_unique<GCOrchestrator>(controller_.get());
    }

    void TearDown() override {
        orchestrator_->WaitForCompletion();
        orchestrator_.reset();
        controller_.reset();
        heap_.reset();
    }

    std::unique_ptr<GCHeap> heap_;
    std::unique_ptr<GCController> controller_;
    std::unique_ptr<GCOrchestrator> orchestrator_;

    GCCell* Allocate(size_t size) {
        return heap_->AllocateRaw(size, AllocationSpace::kNewSpace);
    }
};

TEST_F(GCStressTest, Allocate80kObjects) {
    std::vector<GCCell*> objects;
    objects.reserve(85000);
    
    for (int i = 0; i < 85000; i++) {
        objects.push_back(Allocate(32));
    }
    
    EXPECT_EQ(objects.size(), 85000);
    EXPECT_GE(heap_->GetAllocatedBytes(), 85000 * 32);
}

TEST_F(GCStressTest, TriggerConcurrentGCUnderPressure) {
    for (int i = 0; i < 50000; i++) {
        Allocate(64);
    }
    
    orchestrator_->TriggerGarbageCollection(GCType::kConcurrentMarkSweep);
    
    for (int i = 0; i < 10000; i++) {
         Allocate(16);
    }
    
    orchestrator_->WaitForCompletion();
    EXPECT_FALSE(controller_->IsGCRunning());
}

TEST_F(GCStressTest, NoLeaksAfterHeavyAllocationAndFree) {
    size_t initial_mem = heap_->GetAllocatedBytes();
    
    {
        for (int i = 0; i < 100000; i++) {
            Allocate(64);
        }
    }
    
    orchestrator_->TriggerGarbageCollection(GCType::kFullGC);
    orchestrator_->WaitForCompletion();
    
    size_t final_mem = heap_->GetAllocatedBytes();
    EXPECT_EQ(initial_mem, final_mem);
}

TEST_F(GCStressTest, MultithreadedAllocationPressure) {
    auto worker = [&]() {
        for (int i = 0; i < 10000; i++) {
            Allocate(32);
        }
    };
    
    std::thread t1(worker), t2(worker), t3(worker), t4(worker);
    t1.join(); t2.join(); t3.join(); t4.join();
    
    EXPECT_GT(heap_->GetAllocatedBytes(), 0);
}

TEST_F(GCStressTest, VerifyHeapIntegrityDuringStress) {
    GCVerifier verifier(heap_.get());
    for (int i = 0; i < 50000; i++) {
        Allocate(64);
        if (i % 10000 == 0) {
            EXPECT_TRUE(verifier.Verify());
        }
    }
}

TEST_F(GCStressTest, LargeObjectAllocationPressure) {
    std::vector<GCCell*> large_objects;
    for(int i = 0; i < 100; i++) {
        large_objects.push_back(heap_->AllocateRaw(1024 * 1024, AllocationSpace::kLargeObjectSpace)); // 1MB objects
    }
    EXPECT_EQ(large_objects.size(), 100);
    orchestrator_->TriggerGarbageCollection(GCType::kFullGC);
    orchestrator_->WaitForCompletion();
}

TEST_F(GCStressTest, EphemeralRapidTurnaround) {
    for (int cycle = 0; cycle < 100; cycle++) {
        for (int i = 0; i < 1000; i++) {
            Allocate(16);
        }
        orchestrator_->TriggerGarbageCollection(GCType::kScavenge);
        orchestrator_->WaitForCompletion();
    }
    EXPECT_LT(heap_->GetAllocatedBytes(), 1024 * 1024 * 10);
}

TEST_F(GCStressTest, FragmentationResistance) {
    std::vector<GCCell*> kept_objects;
    for (int i = 0; i < 80000; i++) {
        GCCell* obj = Allocate(64);
        if (i % 3 == 0) kept_objects.push_back(obj);
    }
    
    orchestrator_->TriggerGarbageCollection(GCType::kFullGC);
    orchestrator_->WaitForCompletion();
    
    GCCell* large_obj = heap_->AllocateRaw(1024 * 64, AllocationSpace::kOldSpace);
    EXPECT_NE(large_obj, nullptr);
}

TEST_F(GCStressTest, SurviveMultipleCollections) {
    GCCell* survivor = Allocate(128);
    // Pin object so it's treated as root
    heap_->PinObject(survivor);

    for (int i = 0; i < 10; i++) {
        Allocate(1024);
        orchestrator_->TriggerGarbageCollection(GCType::kScavenge);
        orchestrator_->WaitForCompletion();
    }
    
    EXPECT_TRUE(heap_->IsAllocated(survivor));
    heap_->UnpinObject(survivor);
}

TEST_F(GCStressTest, HighPromotionRateUnderPressure) {
    std::vector<GCCell*> kept_objects;
    for (int cycle = 0; cycle < 10; cycle++) {
        for (int i = 0; i < 5000; i++) {
            GCCell* obj = Allocate(16);
            if (i % 5 == 0) kept_objects.push_back(obj);
        }
        orchestrator_->TriggerGarbageCollection(GCType::kScavenge);
        orchestrator_->WaitForCompletion();
    }
    EXPECT_GT(heap_->GetOldSpace()->GetAllocatedBytes(), 0);
}

TEST_F(GCStressTest, MemoryExhaustionPrevention) {
    bool oom_triggered = false;
    try {
        for (int i = 0; i < 1000000; i++) {
            Allocate(1024 * 1024); // Requesting tons of memory
        }
    } catch (const std::bad_alloc&) {
        oom_triggered = true;
    }
    EXPECT_TRUE(oom_triggered);
}

TEST_F(GCStressTest, WeakReferenceStressSweep) {
    for (int i = 0; i < 50000; i++) {
        Allocate(32);
    }
    orchestrator_->TriggerGarbageCollection(GCType::kConcurrentMarkSweep);
    orchestrator_->WaitForCompletion();
    EXPECT_LT(heap_->GetAllocatedBytes(), 1024 * 1024 * 2);
}

} // namespace testing
} // namespace heap
} // namespace Zepra
