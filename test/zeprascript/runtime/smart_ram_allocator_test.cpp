// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include <gtest/gtest.h>
#include <vector>
#include <memory>

#include "runtime/smart_ram_allocator.h"
#include "runtime/memory_pressure.h"
#include "heap/gc_heap.hpp"

namespace Zepra {
namespace runtime {
namespace testing {

class SmartRamAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = std::make_unique<SmartRamAllocator>(1024 * 1024 * 1024); // 1GB system RAM limit
    }

    void TearDown() override {
        allocator_.reset();
    }

    std::unique_ptr<SmartRamAllocator> allocator_;
};

TEST_F(SmartRamAllocatorTest, InitialBudgetDistribution) {
    allocator_->RegisterTab(1);
    allocator_->RegisterTab(2);
    
    size_t budget1 = allocator_->GetTabBudget(1);
    size_t budget2 = allocator_->GetTabBudget(2);
    
    EXPECT_GT(budget1, 0);
    EXPECT_GT(budget2, 0);
    EXPECT_LE(budget1 + budget2, allocator_->GetTotalSystemMemory());
}

TEST_F(SmartRamAllocatorTest, DynamicReallocationOnNewTab) {
    allocator_->RegisterTab(1);
    size_t initial_budget = allocator_->GetTabBudget(1);
    
    allocator_->RegisterTab(2);
    size_t new_budget = allocator_->GetTabBudget(1);
    
    EXPECT_LT(new_budget, initial_budget); // Budget should decrease to accommodate new tab
}

TEST_F(SmartRamAllocatorTest, ActiveTabPrioritization) {
    allocator_->RegisterTab(1);
    allocator_->RegisterTab(2);
    
    allocator_->SetForegroundTab(1);
    
    size_t active_budget = allocator_->GetTabBudget(1);
    size_t background_budget = allocator_->GetTabBudget(2);
    
    EXPECT_GT(active_budget, background_budget);
}

TEST_F(SmartRamAllocatorTest, MemoryPressureSignalSent) {
    allocator_->RegisterTab(1);
    bool signal_received = false;
    
    allocator_->OnMemoryPressure([&](int tab_id, MemoryPressureLevel level) {
        if (tab_id == 1 && level == MemoryPressureLevel::kCritical) {
            signal_received = true;
        }
    });

    // Simulate allocating almost all RAM
    allocator_->SimulateAllocation(1, allocator_->GetTabBudget(1) * 0.95);
    allocator_->ProcessPressureSignals();
    
    EXPECT_TRUE(signal_received);
}

TEST_F(SmartRamAllocatorTest, BackgroundTabSuspensionOnLowMemory) {
    allocator_->RegisterTab(1); // foreground
    allocator_->RegisterTab(2); // background
    
    allocator_->SetForegroundTab(1);
    bool bg_suspended = false;
    
    allocator_->OnTabSuspendRequest([&](int tab_id) {
        if (tab_id == 2) bg_suspended = true;
    });

    allocator_->SimulateSystemMemoryPressure(MemoryPressureLevel::kCritical);
    
    EXPECT_TRUE(bg_suspended);
}

TEST_F(SmartRamAllocatorTest, FreedMemoryRedistribution) {
    allocator_->RegisterTab(1);
    allocator_->RegisterTab(2);
    
    size_t old_budget2 = allocator_->GetTabBudget(2);
    allocator_->UnregisterTab(1); // Tab 1 closed
    
    size_t new_budget2 = allocator_->GetTabBudget(2);
    EXPECT_GT(new_budget2, old_budget2);
}

TEST_F(SmartRamAllocatorTest, LimitHardCapRespected) {
    allocator_->RegisterTab(1);
    size_t budget = allocator_->GetTabBudget(1);
    
    EXPECT_TRUE(allocator_->TryAllocate(1, budget * 0.5));
    EXPECT_FALSE(allocator_->TryAllocate(1, budget)); // total would be 1.5x budget
}

TEST_F(SmartRamAllocatorTest, MediaTabGetsBonusBudget) {
    allocator_->RegisterTab(1);
    allocator_->RegisterTab(2);
    
    allocator_->SetTabRole(2, TabRole::kMediaPlaying);
    
    EXPECT_GT(allocator_->GetTabBudget(2), allocator_->GetTabBudget(1));
}

TEST_F(SmartRamAllocatorTest, ModeratePressureTriggersGC) {
    allocator_->RegisterTab(1);
    bool gc_triggered = false;
    
    allocator_->OnGCTriggerRequest([&](int tab_id, GCType type) {
        if (tab_id == 1 && type == GCType::kScavenge) {
            gc_triggered = true;
        }
    });
    
    allocator_->SimulateAllocation(1, allocator_->GetTabBudget(1) * 0.75);
    allocator_->ProcessPressureSignals();
    
    EXPECT_TRUE(gc_triggered);
}

TEST_F(SmartRamAllocatorTest, SmoothDegradationMultipleTabs) {
    for(int i = 0; i < 50; i++) {
        allocator_->RegisterTab(i);
    }
    
    for(int i = 0; i < 50; i++) {
        EXPECT_GT(allocator_->GetTabBudget(i), 0);
    }
}

TEST_F(SmartRamAllocatorTest, UnregisterNonExistentTab) {
    EXPECT_NO_THROW(allocator_->UnregisterTab(999));
}

TEST_F(SmartRamAllocatorTest, SetForegroundNonExistentTab) {
    EXPECT_FALSE(allocator_->SetForegroundTab(999));
}

} // namespace testing
} // namespace runtime
} // namespace Zepra
