// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include <gtest/gtest.h>
#include <vector>
#include <memory>

#include "heap/gc_heap.hpp"
#include "heap/generational_gc.h"
#include "heap/Nursery.h"
#include "heap/OldGeneration.h"
#include "heap/WriteBarrier.h"
#include "heap/RememberedSet.h"
#include "heap/GCController.h"
#include "heap/gc_cell.h"

namespace Zepra {
namespace heap {
namespace testing {

class GCGenerationalTest : public ::testing::Test {
protected:
    void SetUp() override {
        heap_ = std::make_unique<GCHeap>(256 * 1024 * 1024); 
        controller_ = std::make_unique<GCController>(heap_.get());
        nursery_ = heap_->GetNursery();
        old_gen_ = heap_->GetOldGeneration();
        write_barrier_ = heap_->GetWriteBarrier();
    }

    void TearDown() override {
        controller_.reset();
        heap_.reset();
    }

    std::unique_ptr<GCHeap> heap_;
    std::unique_ptr<GCController> controller_;
    Nursery* nursery_;
    OldGeneration* old_gen_;
    WriteBarrier* write_barrier_;

    GCCell* AllocateYoung(size_t size) {
        return heap_->AllocateRaw(size, AllocationSpace::kNewSpace);
    }

    GCCell* AllocateOld(size_t size) {
        return heap_->AllocateRaw(size, AllocationSpace::kOldSpace);
    }
};

TEST_F(GCGenerationalTest, InitialAllocationInNursery) {
    GCCell* obj = AllocateYoung(64);
    EXPECT_TRUE(nursery_->Contains(obj));
    EXPECT_FALSE(old_gen_->Contains(obj));
}

TEST_F(GCGenerationalTest, PromoteToOldGenerationOnScavenge) {
    GCCell* survivor = AllocateYoung(64);
    heap_->PinObject(survivor); // Keep alive

    controller_->TriggerScavenge();
    controller_->WaitForCompletion();

    // Check if it got promoted
    EXPECT_FALSE(nursery_->Contains(survivor));
    EXPECT_TRUE(old_gen_->Contains(survivor));
    heap_->UnpinObject(survivor);
}

TEST_F(GCGenerationalTest, OldToYoungPointerWriteBarrier) {
    GCCell* old_obj = AllocateOld(64);
    GCCell* young_obj = AllocateYoung(64);

    // Simulate writing young_obj reference into old_obj
    write_barrier_->MarkObjectModification(old_obj, young_obj);

    EXPECT_TRUE(heap_->GetRememberedSet()->IsRecorded(old_obj));
}

TEST_F(GCGenerationalTest, ScavengeFollowsRememberedSet) {
    GCCell* old_obj = AllocateOld(64);
    GCCell* young_obj = AllocateYoung(64);

    heap_->PinObject(old_obj); // Old obj is root
    // young_obj is only referenced by old_obj
    write_barrier_->MarkObjectModification(old_obj, young_obj);

    controller_->TriggerScavenge();
    controller_->WaitForCompletion();

    // young_obj must be promoted since old_obj holds a ref to it
    EXPECT_TRUE(old_gen_->Contains(young_obj));
    heap_->UnpinObject(old_obj);
}

TEST_F(GCGenerationalTest, WriteBarrierDisabledForYoungToYoung) {
    GCCell* young1 = AllocateYoung(64);
    GCCell* young2 = AllocateYoung(64);

    // Write barrier may be invoked but shouldn't record remembered set
    write_barrier_->MarkObjectModification(young1, young2);
    EXPECT_FALSE(heap_->GetRememberedSet()->IsRecorded(young1));
}

TEST_F(GCGenerationalTest, NurseryEvacuationClearsSpace) {
    for (int i = 0; i < 1000; i++) {
        AllocateYoung(32);
    }
    
    size_t before_scavenge = nursery_->GetAllocatedBytes();
    EXPECT_GT(before_scavenge, 0);

    controller_->TriggerScavenge();
    controller_->WaitForCompletion();

    EXPECT_EQ(nursery_->GetAllocatedBytes(), 0); // No pinned objects
}

TEST_F(GCGenerationalTest, OldGenerationCollectedOnlyOnFullGC) {
    GCCell* old_obj = AllocateOld(64);
    
    controller_->TriggerScavenge();
    controller_->WaitForCompletion();
    
    EXPECT_TRUE(heap_->IsAllocated(old_obj)); // Scavenge doesn't touch old gen
    
    controller_->TriggerFullGC();
    controller_->WaitForCompletion();
    
    EXPECT_FALSE(heap_->IsAllocated(old_obj)); // Full GC collects it
}

TEST_F(GCGenerationalTest, PretenuringLargeObjects) {
    GCCell* large_obj = heap_->AllocateRaw(2 * 1024 * 1024, AllocationSpace::kNewSpace); 
    // Large objects should go straight to LargeObjectSpace or OldSpace, skipping nursery
    EXPECT_FALSE(nursery_->Contains(large_obj));
}

TEST_F(GCGenerationalTest, RememberedSetClearedAfterFullGC) {
    GCCell* old_obj = AllocateOld(64);
    GCCell* young_obj = AllocateYoung(64);

    write_barrier_->MarkObjectModification(old_obj, young_obj);
    EXPECT_TRUE(heap_->GetRememberedSet()->IsRecorded(old_obj));

    controller_->TriggerFullGC();
    controller_->WaitForCompletion();

    // Remembered set should be clean after full GC
    EXPECT_FALSE(heap_->GetRememberedSet()->IsRecorded(old_obj));
}

TEST_F(GCGenerationalTest, TenuringThresholdRespected) {
    GCCell* obj = AllocateYoung(32);
    heap_->PinObject(obj);

    // Simulate multiple survivals, but threshold is 2
    for(int i = 0; i < 2; i++) {
        EXPECT_TRUE(nursery_->Contains(obj));
        controller_->TriggerScavenge();
        controller_->WaitForCompletion();
    }
    
    EXPECT_TRUE(old_gen_->Contains(obj));
    heap_->UnpinObject(obj);
}

TEST_F(GCGenerationalTest, GenerationalGCMetricsRecorded) {
    for(int i = 0; i < 50; i++) AllocateYoung(64);
    
    controller_->TriggerScavenge();
    controller_->WaitForCompletion();
    
    auto metrics = controller_->GetMetrics();
    EXPECT_GT(metrics.scavenge_count, 0);
    EXPECT_EQ(metrics.full_gc_count, 0);
}

TEST_F(GCGenerationalTest, CrossRegionPointerMarking) {
    GCCell* old1 = AllocateOld(32);
    GCCell* young1 = AllocateYoung(32);
    
    heap_->PinObject(old1);
    write_barrier_->MarkObjectModification(old1, young1);
    
    controller_->TriggerScavenge();
    controller_->WaitForCompletion();
    
    EXPECT_TRUE(old_gen_->Contains(young1));
    heap_->UnpinObject(old1);
}

} // namespace testing
} // namespace heap
} // namespace Zepra
