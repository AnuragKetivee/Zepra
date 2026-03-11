// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <chrono>

#include "runtime/gc_tab_lifecycle.h"
#include "runtime/execution_context.h"
#include "heap/gc_heap.hpp"

namespace Zepra {
namespace runtime {
namespace testing {

class TabLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        heap_ = std::make_unique<heap::GCHeap>(256 * 1024 * 1024);
        manager_ = std::make_unique<TabLifecycleManager>(heap_.get());
    }

    void TearDown() override {
        manager_.reset();
        heap_.reset();
    }

    std::unique_ptr<heap::GCHeap> heap_;
    std::unique_ptr<TabLifecycleManager> manager_;
};

TEST_F(TabLifecycleTest, CreateTabContext) {
    int tab_id = manager_->CreateTab("https://ketivee.com");
    EXPECT_GT(tab_id, 0);
    EXPECT_EQ(manager_->GetTabState(tab_id), TabState::kActive);
}

TEST_F(TabLifecycleTest, SuspendActiveTab) {
    int tab_id = manager_->CreateTab("https://example.com");
    EXPECT_TRUE(manager_->SuspendTab(tab_id));
    EXPECT_EQ(manager_->GetTabState(tab_id), TabState::kSuspended);
}

TEST_F(TabLifecycleTest, ResumeSuspendedTab) {
    int tab_id = manager_->CreateTab("https://example.com");
    manager_->SuspendTab(tab_id);
    EXPECT_TRUE(manager_->ResumeTab(tab_id));
    EXPECT_EQ(manager_->GetTabState(tab_id), TabState::kActive);
}

TEST_F(TabLifecycleTest, DestroyTabContext) {
    int tab_id = manager_->CreateTab("https://example.com");
    EXPECT_TRUE(manager_->DestroyTab(tab_id));
    EXPECT_EQ(manager_->GetTabState(tab_id), TabState::kNonExistent);
}

TEST_F(TabLifecycleTest, SuspendAlreadySuspendedTab) {
    int tab_id = manager_->CreateTab("https://example.com");
    manager_->SuspendTab(tab_id);
    EXPECT_FALSE(manager_->SuspendTab(tab_id)); // Should return false
}

TEST_F(TabLifecycleTest, ResumeActiveTab) {
    int tab_id = manager_->CreateTab("https://example.com");
    EXPECT_FALSE(manager_->ResumeTab(tab_id)); // Should return false
}

TEST_F(TabLifecycleTest, DestroyNonExistentTab) {
    EXPECT_FALSE(manager_->DestroyTab(999));
}

TEST_F(TabLifecycleTest, SuspendFreesEphemeralMemory) {
    int tab_id = manager_->CreateTab("https://example.com");
    
    heap_->AllocateRaw(1024 * 1024, heap::AllocationSpace::kNewSpace); 
    
    size_t mem_before = heap_->GetAllocatedBytes();
    manager_->SuspendTab(tab_id);
    
    size_t mem_after = heap_->GetAllocatedBytes();
    EXPECT_LT(mem_after, mem_before);
}

TEST_F(TabLifecycleTest, MultipleTabsIndependentLifecycle) {
    int t1 = manager_->CreateTab("https://a.com");
    int t2 = manager_->CreateTab("https://b.com");
    
    manager_->SuspendTab(t1);
    
    EXPECT_EQ(manager_->GetTabState(t1), TabState::kSuspended);
    EXPECT_EQ(manager_->GetTabState(t2), TabState::kActive);
}

TEST_F(TabLifecycleTest, TabOriginReporting) {
    int tab_id = manager_->CreateTab("https://secure.com");
    EXPECT_EQ(manager_->GetOrigin(tab_id), "https://secure.com");
}

TEST_F(TabLifecycleTest, TabTimeoutToSuspension) {
    int tab_id = manager_->CreateTab("https://example.com");
    manager_->MarkTabIdle(tab_id, std::chrono::minutes(15));
    
    manager_->ProcessIdleTabs();
    EXPECT_EQ(manager_->GetTabState(tab_id), TabState::kSuspended);
}

TEST_F(TabLifecycleTest, DiscardSuspendedTabUnderPressure) {
    int tab_id = manager_->CreateTab("https://example.com");
    manager_->SuspendTab(tab_id);
    
    manager_->HandleSystemMemoryPressure(MemoryPressureLevel::kCritical);
    EXPECT_EQ(manager_->GetTabState(tab_id), TabState::kDiscarded);
}

} // namespace testing
} // namespace runtime
} // namespace Zepra
