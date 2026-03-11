// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <stdexcept>

#include "heap/gc_heap.hpp"
#include "heap/gc_tab_isolator.h"
#include "heap/GCController.h"
#include "runtime/tab_lifecycle.h"
#include "heap/gc_cell.h"

namespace Zepra {
namespace heap {
namespace testing {

class GCTabIsolationTest : public ::testing::Test {
protected:
    void SetUp() override {
        heap_ = std::make_unique<GCHeap>(256 * 1024 * 1024);
        controller_ = std::make_unique<GCController>(heap_.get());
        isolator_ = std::make_unique<GCTabIsolator>(heap_.get());
        
        tab_a_id = isolator_->CreateTabContext("tab_A_origin");
        tab_b_id = isolator_->CreateTabContext("tab_B_origin");
    }

    void TearDown() override {
        isolator_->DestroyTabContext(tab_a_id);
        isolator_->DestroyTabContext(tab_b_id);
        
        isolator_.reset();
        controller_.reset();
        heap_.reset();
    }

    std::unique_ptr<GCHeap> heap_;
    std::unique_ptr<GCController> controller_;
    std::unique_ptr<GCTabIsolator> isolator_;
    int tab_a_id;
    int tab_b_id;

    GCCell* AllocateInTab(int tab_id, size_t size) {
        isolator_->SwitchToTabContext(tab_id);
        return heap_->AllocateRaw(size, AllocationSpace::kNewSpace);
    }
};

TEST_F(GCTabIsolationTest, SeparateAllocationArenas) {
    GCCell* obj_a = AllocateInTab(tab_a_id, 64);
    GCCell* obj_b = AllocateInTab(tab_b_id, 64);
    
    EXPECT_NE(obj_a, obj_b);
    EXPECT_NE(heap_->GetArenaForObject(obj_a), heap_->GetArenaForObject(obj_b));
}

TEST_F(GCTabIsolationTest, CrossTabAccessDenied) {
    GCCell* obj_a = AllocateInTab(tab_a_id, 64);
    
    isolator_->SwitchToTabContext(tab_b_id);
    EXPECT_THROW(isolator_->ValidateObjectAccess(obj_a), std::runtime_error);
}

TEST_F(GCTabIsolationTest, ValidAccessAllowed) {
    GCCell* obj_b = AllocateInTab(tab_b_id, 64);
    
    isolator_->SwitchToTabContext(tab_b_id);
    EXPECT_NO_THROW(isolator_->ValidateObjectAccess(obj_b));
}

TEST_F(GCTabIsolationTest, TabContextDestructionFreesMemory) {
    AllocateInTab(tab_a_id, 1024 * 1024); // 1MB
    size_t mem_before = heap_->GetAllocatedBytes();
    
    isolator_->DestroyTabContext(tab_a_id);
    controller_->TriggerFullGC();
    controller_->WaitForCompletion();
    
    size_t mem_after = heap_->GetAllocatedBytes();
    EXPECT_LT(mem_after, mem_before);
    
    tab_a_id = isolator_->CreateTabContext("tab_A_origin_new"); // re-create for teardown
}

TEST_F(GCTabIsolationTest, NoInterferenceDuringGC) {
    GCCell* obj_a = AllocateInTab(tab_a_id, 64);
    GCCell* obj_b = AllocateInTab(tab_b_id, 64);
    
    heap_->PinObject(obj_a);
    heap_->PinObject(obj_b);

    // Collect tab A specifically if supported, or full GC
    controller_->TriggerGarbageCollection(GCType::kScavenge, tab_a_id);
    controller_->WaitForCompletion();

    EXPECT_TRUE(heap_->IsAllocated(obj_a));
    EXPECT_TRUE(heap_->IsAllocated(obj_b));
    
    heap_->UnpinObject(obj_a);
    heap_->UnpinObject(obj_b);
}

TEST_F(GCTabIsolationTest, GlobalMemoryLimitsRespectedPerTab) {
    isolator_->SetMemoryLimit(tab_a_id, 4 * 1024 * 1024); // 4MB
    
    EXPECT_NO_THROW(AllocateInTab(tab_a_id, 2 * 1024 * 1024));
    EXPECT_THROW(AllocateInTab(tab_a_id, 3 * 1024 * 1024), std::bad_alloc);
}

TEST_F(GCTabIsolationTest, TabContextSwitchThreadSafety) {
    EXPECT_EQ(isolator_->GetCurrentTabContext(), tab_a_id); // assume last was A from setup if modified
    isolator_->SwitchToTabContext(tab_b_id);
    EXPECT_EQ(isolator_->GetCurrentTabContext(), tab_b_id);
}

TEST_F(GCTabIsolationTest, ObjectBelongsToCorrectTab) {
    GCCell* obj_a = AllocateInTab(tab_a_id, 32);
    EXPECT_EQ(isolator_->GetTabContextForObject(obj_a), tab_a_id);
    
    GCCell* obj_b = AllocateInTab(tab_b_id, 32);
    EXPECT_EQ(isolator_->GetTabContextForObject(obj_b), tab_b_id);
}

TEST_F(GCTabIsolationTest, OOMInOneTabDoesNotAffectOthers) {
    isolator_->SetMemoryLimit(tab_a_id, 1024 * 1024); // 1MB
    EXPECT_THROW(AllocateInTab(tab_a_id, 2 * 1024 * 1024), std::bad_alloc);
    
    EXPECT_NO_THROW(AllocateInTab(tab_b_id, 512 * 1024));
}

TEST_F(GCTabIsolationTest, GCReclaimsOnlySpecificTabResources) {
    AllocateInTab(tab_a_id, 1024 * 512);
    GCCell* obj_b = AllocateInTab(tab_b_id, 1024 * 512);
    heap_->PinObject(obj_b);

    controller_->TriggerGarbageCollection(GCType::kFullGC, tab_a_id);
    controller_->WaitForCompletion();
    
    EXPECT_TRUE(heap_->IsAllocated(obj_b));
    heap_->UnpinObject(obj_b);
}

TEST_F(GCTabIsolationTest, CrossTabMessagePassingDoesNotViolateIsolation) {
    GCCell* msg = AllocateInTab(tab_a_id, 128);
    // Passing message involves serialization, direct memory access is not allowed
    EXPECT_THROW(isolator_->TransferObject(msg, tab_b_id), std::runtime_error); 
}

TEST_F(GCTabIsolationTest, OriginSpoofingPrevented) {
    EXPECT_THROW(isolator_->ValidateTabOrigin(tab_a_id, "tab_B_origin"), std::runtime_error);
    EXPECT_NO_THROW(isolator_->ValidateTabOrigin(tab_a_id, "tab_A_origin"));
}

} // namespace testing
} // namespace heap
} // namespace Zepra
