// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

/**
 * @file sandbox_tests.cpp
 * @brief Unit tests for Sandbox and Security Infrastructure
 */

#include <gtest/gtest.h>
#include "runtime/execution/Sandbox.h"
#include "runtime/execution/IsolatedGlobal.h"
#include <chrono>
#include <thread>
#include <unordered_map>

using namespace Zepra::Runtime;

// =============================================================================
// ExecutionLimits Tests
// =============================================================================

class ExecutionLimitsTests : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ExecutionLimitsTests, DefaultLimits) {
    ExecutionLimits limits;
    
    EXPECT_EQ(limits.maxHeapBytes, 256 * 1024 * 1024);
    EXPECT_EQ(limits.maxStackBytes, 8 * 1024 * 1024);
    EXPECT_EQ(limits.maxCallStackDepth, 10000);
}

TEST_F(ExecutionLimitsTests, BrowserLimits) {
    auto limits = ExecutionLimits::browser();
    
    EXPECT_EQ(limits.maxHeapBytes, 512 * 1024 * 1024);
    EXPECT_EQ(limits.maxExecutionTime, std::chrono::seconds(30));
}

TEST_F(ExecutionLimitsTests, UntrustedLimits) {
    auto limits = ExecutionLimits::untrusted();
    
    EXPECT_EQ(limits.maxHeapBytes, 64 * 1024 * 1024);
    EXPECT_EQ(limits.maxExecutionTime, std::chrono::seconds(5));
    EXPECT_EQ(limits.maxCallStackDepth, 1000);
}

// =============================================================================
// SecurityPolicy Tests
// =============================================================================

class SecurityPolicyTests : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(SecurityPolicyTests, BrowserPolicy) {
    auto policy = SecurityPolicy::browser();
    
    EXPECT_FALSE(policy.allowEval);
    EXPECT_TRUE(policy.allowFetch);
    EXPECT_TRUE(policy.allowWorkers);
    EXPECT_TRUE(policy.allowWasm);
}

TEST_F(SecurityPolicyTests, StrictPolicy) {
    auto policy = SecurityPolicy::strict();
    
    EXPECT_FALSE(policy.allowEval);
    EXPECT_FALSE(policy.allowFetch);
    EXPECT_FALSE(policy.allowWorkers);
    EXPECT_FALSE(policy.allowWasm);
    EXPECT_FALSE(policy.allowNetworkAccess);
}

// =============================================================================
// ResourceMonitor Tests
// =============================================================================

class ResourceMonitorTests : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ResourceMonitorTests, InstructionCounting) {
    auto limits = ExecutionLimits::browser();
    ResourceMonitor monitor(limits);
    
    EXPECT_EQ(monitor.instructionCount(), 0);
    
    monitor.addInstructions(1000);
    EXPECT_EQ(monitor.instructionCount(), 1000);
    
    monitor.addInstructions(500);
    EXPECT_EQ(monitor.instructionCount(), 1500);
}

TEST_F(ResourceMonitorTests, InstructionLimit) {
    ExecutionLimits limits;
    limits.maxInstructions = 1000;
    ResourceMonitor monitor(limits);
    
    EXPECT_TRUE(monitor.checkInstructionLimit());
    
    monitor.addInstructions(500);
    EXPECT_TRUE(monitor.checkInstructionLimit());
    
    monitor.addInstructions(600);  // Now at 1100
    EXPECT_FALSE(monitor.checkInstructionLimit());
}

TEST_F(ResourceMonitorTests, HeapTracking) {
    auto limits = ExecutionLimits::browser();
    ResourceMonitor monitor(limits);
    
    EXPECT_EQ(monitor.heapUsed(), 0);
    
    monitor.addHeapAllocation(1024);
    EXPECT_EQ(monitor.heapUsed(), 1024);
    
    monitor.removeHeapAllocation(512);
    EXPECT_EQ(monitor.heapUsed(), 512);
}

TEST_F(ResourceMonitorTests, HeapLimit) {
    ExecutionLimits limits;
    limits.maxHeapBytes = 1024;
    ResourceMonitor monitor(limits);
    
    EXPECT_TRUE(monitor.checkHeapLimit());
    
    monitor.addHeapAllocation(512);
    EXPECT_TRUE(monitor.checkHeapLimit());
    
    monitor.addHeapAllocation(600);  // Now at 1112
    EXPECT_FALSE(monitor.checkHeapLimit());
}

TEST_F(ResourceMonitorTests, CallStackTracking) {
    auto limits = ExecutionLimits::browser();
    ResourceMonitor monitor(limits);
    
    EXPECT_EQ(monitor.callDepth(), 0);
    
    monitor.pushCall();
    monitor.pushCall();
    EXPECT_EQ(monitor.callDepth(), 2);
    
    monitor.popCall();
    EXPECT_EQ(monitor.callDepth(), 1);
}

TEST_F(ResourceMonitorTests, StackLimit) {
    ExecutionLimits limits;
    limits.maxCallStackDepth = 3;
    ResourceMonitor monitor(limits);
    
    EXPECT_TRUE(monitor.checkStackLimit());
    
    monitor.pushCall();
    monitor.pushCall();
    EXPECT_TRUE(monitor.checkStackLimit());
    
    monitor.pushCall();
    monitor.pushCall();  // Now at 4
    EXPECT_FALSE(monitor.checkStackLimit());
}

TEST_F(ResourceMonitorTests, TerminationRequest) {
    auto limits = ExecutionLimits::browser();
    ResourceMonitor monitor(limits);
    
    EXPECT_FALSE(monitor.isTerminationRequested());
    
    monitor.requestTermination();
    EXPECT_TRUE(monitor.isTerminationRequested());
}

// =============================================================================
// IsolatedGlobal Tests
// =============================================================================

class IsolatedGlobalTests : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(IsolatedGlobalTests, SafeGlobalsAllowed) {
    auto policy = SecurityPolicy::browser();
    IsolatedGlobal global(policy);
    
    EXPECT_TRUE(global.isAllowed("Object"));
    EXPECT_TRUE(global.isAllowed("Array"));
    EXPECT_TRUE(global.isAllowed("String"));
    EXPECT_TRUE(global.isAllowed("Math"));
    EXPECT_TRUE(global.isAllowed("JSON"));
}

TEST_F(IsolatedGlobalTests, DangerousGlobalsBlocked) {
    auto policy = SecurityPolicy::browser();
    IsolatedGlobal global(policy);
    
    EXPECT_FALSE(global.isAllowed("eval"));
    EXPECT_FALSE(global.isAllowed("Function"));
    EXPECT_FALSE(global.isAllowed("__proto__"));
}

TEST_F(IsolatedGlobalTests, ExplicitDenyList) {
    SecurityPolicy policy;
    policy.deniedGlobals.insert("console");
    IsolatedGlobal global(policy);
    
    EXPECT_FALSE(global.isAllowed("console"));
}

TEST_F(IsolatedGlobalTests, EvalControl) {
    {
        SecurityPolicy policy;
        policy.allowEval = false;
        IsolatedGlobal global(policy);
        EXPECT_FALSE(global.isEvalAllowed());
    }
    {
        SecurityPolicy policy;
        policy.allowEval = true;
        IsolatedGlobal global(policy);
        EXPECT_TRUE(global.isEvalAllowed());
    }
}

TEST_F(IsolatedGlobalTests, FetchControl) {
    {
        SecurityPolicy policy;
        policy.allowFetch = false;
        IsolatedGlobal global(policy);
        EXPECT_FALSE(global.isFetchAllowed());
    }
    {
        SecurityPolicy policy;
        policy.allowFetch = true;
        IsolatedGlobal global(policy);
        EXPECT_TRUE(global.isFetchAllowed());
    }
}

// =============================================================================
// SecureContext Tests
// =============================================================================

class SecureContextTests : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(SecureContextTests, CreationWithBrowserConfig) {
    auto config = SandboxConfig::browser();
    SecureContext ctx(config);
    
    EXPECT_TRUE(ctx.global().isFetchAllowed());
    EXPECT_TRUE(ctx.global().isWorkersAllowed());
}

TEST_F(SecureContextTests, CreationWithStrictConfig) {
    auto config = SandboxConfig::strict();
    SecureContext ctx(config);
    
    EXPECT_FALSE(ctx.global().isFetchAllowed());
    EXPECT_FALSE(ctx.global().isWorkersAllowed());
}

// =============================================================================
// SecurityError Tests  
// =============================================================================

TEST(SecurityErrorTests, TimeoutError) {
    SecurityError error(SecurityError::Type::Timeout, "Execution timed out");
    
    EXPECT_EQ(error.type(), SecurityError::Type::Timeout);
    EXPECT_STREQ(error.what(), "Execution timed out");
}

TEST(SecurityErrorTests, MemoryLimitError) {
    SecurityError error(SecurityError::Type::MemoryLimit, "Heap limit exceeded");
    
    EXPECT_EQ(error.type(), SecurityError::Type::MemoryLimit);
}

// =============================================================================
// Cross-Tab Heap Isolation Tests (Simulated TabIsolator)
// =============================================================================

namespace {

// Minimal TabIsolator sim to verify isolation invariants without
// depending on the full heap stack.
struct SimTabHeap {
    uint64_t tabId;
    uintptr_t base;
    size_t size;
    size_t used;
    bool active;
    bool wiped;

    SimTabHeap() : tabId(0), base(0), size(0), used(0), active(false), wiped(false) {}
};

class SimTabIsolator {
public:
    bool createTab(uint64_t tabId, uintptr_t base, size_t size) {
        if (tabs_.count(tabId)) return false;
        SimTabHeap h;
        h.tabId = tabId;
        h.base = base;
        h.size = size;
        h.used = 0;
        h.active = true;
        h.wiped = false;
        tabs_[tabId] = h;
        return true;
    }

    void destroyTab(uint64_t tabId) {
        auto it = tabs_.find(tabId);
        if (it == tabs_.end()) return;
        it->second.wiped = true;
        it->second.active = false;
        tabs_.erase(it);
    }

    bool belongsToTab(uint64_t tabId, uintptr_t addr) const {
        auto it = tabs_.find(tabId);
        if (it == tabs_.end()) return false;
        return addr >= it->second.base && addr < it->second.base + it->second.size;
    }

    // Cross-tab pointer store validation: dest must not be in another tab.
    bool validateStore(uint64_t srcTabId, uintptr_t destAddr) const {
        for (auto& [id, tab] : tabs_) {
            if (id != srcTabId && tab.active) {
                if (destAddr >= tab.base && destAddr < tab.base + tab.size) {
                    return false;  // Cross-tab reference blocked
                }
            }
        }
        return true;
    }

    size_t tabCount() const { return tabs_.size(); }
    bool isWiped(uint64_t tabId) const {
        auto it = tabs_.find(tabId);
        return it != tabs_.end() ? it->second.wiped : false;
    }

private:
    std::unordered_map<uint64_t, SimTabHeap> tabs_;
};

}  // namespace

class CrossTabIsolationTests : public ::testing::Test {
protected:
    SimTabIsolator isolator;

    void SetUp() override {
        isolator.createTab(1, 0x10000000, 1024 * 1024);  // Tab 1: 1MB at 0x10000000
        isolator.createTab(2, 0x20000000, 1024 * 1024);  // Tab 2: 1MB at 0x20000000
    }
};

TEST_F(CrossTabIsolationTests, AddressBelongsToCorrectTab) {
    EXPECT_TRUE(isolator.belongsToTab(1, 0x10000000));
    EXPECT_TRUE(isolator.belongsToTab(1, 0x10000000 + 512));
    EXPECT_FALSE(isolator.belongsToTab(1, 0x20000000));
    EXPECT_TRUE(isolator.belongsToTab(2, 0x20000000));
    EXPECT_FALSE(isolator.belongsToTab(2, 0x10000000));
}

TEST_F(CrossTabIsolationTests, CrossTabPointerStoreBlocked) {
    uintptr_t addrInTab2 = 0x20000000 + 512;
    // Tab 1 trying to store pointer to Tab 2's heap — must be blocked.
    EXPECT_FALSE(isolator.validateStore(1, addrInTab2));
}

TEST_F(CrossTabIsolationTests, SameTabPointerStoreAllowed) {
    uintptr_t addrInTab1 = 0x10000000 + 512;
    EXPECT_TRUE(isolator.validateStore(1, addrInTab1));
}

TEST_F(CrossTabIsolationTests, StoreToUnmanagedAddressAllowed) {
    uintptr_t stackAddr = 0x7FFF0000;
    EXPECT_TRUE(isolator.validateStore(1, stackAddr));
}

TEST_F(CrossTabIsolationTests, DestroyTabRemovesFromIsolator) {
    EXPECT_EQ(isolator.tabCount(), 2u);
    isolator.destroyTab(1);
    EXPECT_EQ(isolator.tabCount(), 1u);
    EXPECT_FALSE(isolator.belongsToTab(1, 0x10000000));
}

TEST_F(CrossTabIsolationTests, DestroyTabDoesNotAffectOtherTabs) {
    isolator.destroyTab(1);
    EXPECT_TRUE(isolator.belongsToTab(2, 0x20000000 + 100));
}

TEST_F(CrossTabIsolationTests, DuplicateTabIdRejected) {
    EXPECT_FALSE(isolator.createTab(1, 0x30000000, 1024 * 1024));
}

TEST_F(CrossTabIsolationTests, MultipleTabsIsolated) {
    isolator.createTab(3, 0x30000000, 1024 * 1024);
    isolator.createTab(4, 0x40000000, 1024 * 1024);

    // Each tab's address space is disjoint.
    for (uint64_t src = 1; src <= 4; src++) {
        for (uint64_t dst = 1; dst <= 4; dst++) {
            if (src == dst) continue;
            uintptr_t destBase = 0x10000000 * dst;
            EXPECT_FALSE(isolator.validateStore(src, destBase + 256))
                << "Tab " << src << " should not store to tab " << dst;
        }
    }
}

