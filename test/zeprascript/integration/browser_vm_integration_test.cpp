// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

/**
 * @file browser_vm_integration_test.cpp
 * @brief Integration tests for VM ↔ browser subsystem interop.
 *        Context document binding, Worker compile/execute pipeline,
 *        sandbox enforcement, re-entrancy, and DOM isolation.
 */

#include <gtest/gtest.h>
#include "runtime/execution/vm.hpp"
#include "runtime/execution/context.hpp"
#include "runtime/execution/Sandbox.h"
#include "runtime/objects/object.hpp"
#include "runtime/objects/value.hpp"
#include "browser/ServiceWorker.h"
#include "browser/IndexedDBAPI.h"
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>

using namespace Zepra::Runtime;

// =============================================================================
// Context ↔ Document Binding
// =============================================================================

TEST(BrowserVMIntegration, ContextDocumentRoundTrip) {
    VM vm(nullptr);
    Context ctx(&vm);

    EXPECT_EQ(ctx.getDocument(), nullptr);
    EXPECT_EQ(ctx.vm(), &vm);

    // Simulate setting a document (using opaque pointer since Document
    // is not instantiated here — we test the plumbing, not the DOM)
    auto* fakeDoc = reinterpret_cast<Zepra::Browser::Document*>(0xDEADBEEF);
    ctx.setDocument(fakeDoc);
    EXPECT_EQ(ctx.getDocument(), fakeDoc);
}

TEST(BrowserVMIntegration, ContextDocumentNullSafety) {
    VM vm(nullptr);
    Context ctx(&vm);

    ctx.setDocument(nullptr);
    EXPECT_EQ(ctx.getDocument(), nullptr);

    auto* fakeDoc = reinterpret_cast<Zepra::Browser::Document*>(0x1234);
    ctx.setDocument(fakeDoc);
    EXPECT_EQ(ctx.getDocument(), fakeDoc);

    ctx.setDocument(nullptr);
    EXPECT_EQ(ctx.getDocument(), nullptr);
}

// =============================================================================
// VM Compile Pipeline
// =============================================================================

TEST(BrowserVMIntegration, CompileValidSource) {
    VM vm(nullptr);
    void* compiled = vm.compile("var x = 1 + 2;");
    // Valid JS should produce a non-null chunk
    EXPECT_NE(compiled, nullptr);
}

TEST(BrowserVMIntegration, CompileSyntaxError) {
    VM vm(nullptr);
    void* compiled = vm.compile("function {{{ invalid");
    EXPECT_EQ(compiled, nullptr);
}

TEST(BrowserVMIntegration, CompileExecuteCycle) {
    VM vm(nullptr);
    void* compiled = vm.compile("var result = 42;");
    if (compiled) {
        vm.execute(compiled);
        // After execution, 'result' should be in globals
        Value v = vm.getGlobal("result");
        EXPECT_TRUE(v.isNumber());
        EXPECT_EQ(v.asNumber(), 42.0);
    }
}

TEST(BrowserVMIntegration, CompileExecuteLoop100) {
    VM vm(nullptr);

    for (int i = 0; i < 100; ++i) {
        std::string src = "var iter_" + std::to_string(i) + " = " + std::to_string(i) + ";";
        void* compiled = vm.compile(src);
        if (compiled) {
            vm.execute(compiled);
        }
    }

    // Spot-check a few globals
    EXPECT_EQ(vm.getGlobal("iter_0").asNumber(), 0.0);
    EXPECT_EQ(vm.getGlobal("iter_50").asNumber(), 50.0);
    EXPECT_EQ(vm.getGlobal("iter_99").asNumber(), 99.0);
}

TEST(BrowserVMIntegration, ExecuteNullCompiled) {
    VM vm(nullptr);
    // Should not crash
    vm.execute(static_cast<void*>(nullptr));
    EXPECT_TRUE(true);
}

// =============================================================================
// DOM Context Isolation
// =============================================================================

TEST(BrowserVMIntegration, TwoVMContextIsolation) {
    VM vm1(nullptr);
    VM vm2(nullptr);
    Context ctx1(&vm1);
    Context ctx2(&vm2);

    auto* doc1 = reinterpret_cast<Zepra::Browser::Document*>(0xAAAA);
    auto* doc2 = reinterpret_cast<Zepra::Browser::Document*>(0xBBBB);
    ctx1.setDocument(doc1);
    ctx2.setDocument(doc2);

    EXPECT_NE(ctx1.getDocument(), ctx2.getDocument());
    EXPECT_EQ(ctx1.vm(), &vm1);
    EXPECT_EQ(ctx2.vm(), &vm2);

    vm1.setGlobal("shared", Value::number(1));
    vm2.setGlobal("shared", Value::number(2));

    EXPECT_EQ(vm1.getGlobal("shared").asNumber(), 1.0);
    EXPECT_EQ(vm2.getGlobal("shared").asNumber(), 2.0);
}

// =============================================================================
// Sandbox + Browser Limits
// =============================================================================

TEST(BrowserVMIntegration, BrowserSandboxDefaults) {
    auto limits = ExecutionLimits::browser();

    EXPECT_EQ(limits.maxHeapBytes, 512u * 1024 * 1024);
    EXPECT_GT(limits.maxInstructions, 0u);
    EXPECT_GT(limits.maxCallStackDepth, 0u);
}

TEST(BrowserVMIntegration, SandboxPreventsOverAllocation) {
    ExecutionLimits limits;
    limits.maxHeapBytes = 2 * 1024 * 1024;  // 2MB
    ResourceMonitor monitor(limits);

    VM vm(nullptr);
    vm.setSandbox(&monitor);

    monitor.addHeapAllocation(1 * 1024 * 1024);
    EXPECT_FALSE(vm.shouldTerminate());

    monitor.addHeapAllocation(2 * 1024 * 1024);
    EXPECT_TRUE(vm.shouldTerminate());
}

// =============================================================================
// EvaluateInFrame (Debug Expression)
// =============================================================================

TEST(BrowserVMIntegration, EvaluateInFrameEmpty) {
    VM vm(nullptr);
    Context ctx(&vm);

    // Empty expression → undefined
    Value v = vm.evaluateInFrame(0, "");
    EXPECT_TRUE(v.isUndefined());
}

TEST(BrowserVMIntegration, EvaluateInFrameNullContext) {
    VM vm(nullptr);
    // No context set — should return undefined without crash
    Value v = vm.evaluateInFrame(0, "1 + 1");
    EXPECT_TRUE(v.isUndefined());
}

// =============================================================================
// LoadBundledScript Stub
// =============================================================================

TEST(BrowserVMIntegration, LoadBundledScriptStub) {
    VM vm(nullptr);
    std::string content = vm.loadBundledScript("https://example.com/app.js");
    EXPECT_TRUE(content.empty());
}

// =============================================================================
// Concurrent VM + Object Allocation
// =============================================================================

TEST(BrowserVMIntegration, ConcurrentVMAndObjectAlloc) {
    constexpr int THREADS = 4;
    constexpr int OPS = 10000;
    std::atomic<int> done{0};

    auto worker = [&](int id) {
        VM vm(nullptr);
        for (int i = 0; i < OPS; ++i) {
            vm.setGlobal("v", Value::number(static_cast<double>(i)));

            Object* obj = new Object();
            obj->set("tid", Value::number(static_cast<double>(id)));
            delete obj;
        }
        done++;
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back(worker, t);
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(done.load(), THREADS);
}
