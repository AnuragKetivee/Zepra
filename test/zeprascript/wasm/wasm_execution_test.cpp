// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <cstdint>

#include "wasm/wasm_engine.h"
#include "wasm/wasm_module.h"
#include "wasm/wasm_instance.h"

namespace Zepra {
namespace wasm {
namespace testing {

class WasmExecutionTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<WasmEngine>();
    }

    void TearDown() override {
        engine_.reset();
    }

    std::unique_ptr<WasmEngine> engine_;

    std::vector<uint8_t> CreateDummyWasmBinary() {
        return {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    }
    
    std::vector<uint8_t> CreateAddModuleWasmBinary() {
        return {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    }
};

TEST_F(WasmExecutionTest, EngineInitialization) {
    EXPECT_TRUE(engine_->IsInitialized());
}

TEST_F(WasmExecutionTest, LoadValidModule) {
    auto binary = CreateDummyWasmBinary();
    auto module = engine_->Compile(binary.data(), binary.size());
    EXPECT_NE(module, nullptr);
    EXPECT_TRUE(module->IsValid());
}

TEST_F(WasmExecutionTest, LoadInvalidModule) {
    std::vector<uint8_t> invalid = {0x01, 0x02, 0x03, 0x04};
    EXPECT_THROW(engine_->Compile(invalid.data(), invalid.size()), WasmCompilationError);
}

TEST_F(WasmExecutionTest, InstantiateModule) {
    auto binary = CreateDummyWasmBinary();
    auto module = engine_->Compile(binary.data(), binary.size());
    auto instance = module->Instantiate();
    EXPECT_NE(instance, nullptr);
}

TEST_F(WasmExecutionTest, ExecuteExportedFunction) {
    auto binary = CreateAddModuleWasmBinary();
    auto module = engine_->Compile(binary.data(), binary.size());
    auto instance = module->Instantiate();
    
    WasmValue args[] = { WasmValue::I32(5), WasmValue::I32(7) };
    WasmValue result = instance->CallExport("add", args, 2);
    
    EXPECT_TRUE(result.IsI32());
}

TEST_F(WasmExecutionTest, MemoryBoundsCheck) {
    auto binary = CreateDummyWasmBinary();
    auto module = engine_->Compile(binary.data(), binary.size());
    auto instance = module->Instantiate();
    
    auto memory = instance->GetMemory();
    EXPECT_THROW(memory->Read8(memory->GetSize() + 100), WasmTrapBoundsCheck);
}

TEST_F(WasmExecutionTest, FunctionTableAccess) {
    auto binary = CreateDummyWasmBinary();
    auto module = engine_->Compile(binary.data(), binary.size());
    auto instance = module->Instantiate();
    
    auto table = instance->GetTable();
    if (table) {
        EXPECT_THROW(table->Get(table->GetSize() + 1), WasmTrapTableOutOfBounds);
    }
}

TEST_F(WasmExecutionTest, StackOverflowTrap) {
    auto binary = CreateDummyWasmBinary();
    auto module = engine_->Compile(binary.data(), binary.size());
    auto instance = module->Instantiate();
    
    EXPECT_THROW(instance->CallExport("deep_recursion", nullptr, 0), WasmTrapStackOverflow);
}

TEST_F(WasmExecutionTest, DivideByZeroTrap) {
    auto binary = CreateDummyWasmBinary();
    auto module = engine_->Compile(binary.data(), binary.size());
    auto instance = module->Instantiate();
    
    WasmValue args[] = { WasmValue::I32(10), WasmValue::I32(0) };
    EXPECT_THROW(instance->CallExport("divide", args, 2), WasmTrapDivideByZero);
}

TEST_F(WasmExecutionTest, ImportResolution) {
    auto binary = CreateDummyWasmBinary();
    auto module = engine_->Compile(binary.data(), binary.size());
    
    bool called = false;
    module->DefineImport("env", "print", [&](const WasmValue* args, int count) {
        called = true;
        return WasmValue();
    });
    
    auto instance = module->Instantiate();
    instance->CallExport("call_print", nullptr, 0); 
}

TEST_F(WasmExecutionTest, GlobalVariableAccess) {
    auto binary = CreateDummyWasmBinary();
    auto module = engine_->Compile(binary.data(), binary.size());
    auto instance = module->Instantiate();
    
    auto global = instance->GetGlobal("counter");
    if (global) {
        EXPECT_TRUE(global->IsMutable());
        global->Set(WasmValue::I32(42));
        EXPECT_EQ(global->Get().AsI32(), 42);
    }
}

TEST_F(WasmExecutionTest, ConcurrentCompilation) {
    auto binary = CreateDummyWasmBinary();
    EXPECT_NO_THROW({
        engine_->CompileAsync(binary.data(), binary.size(), [](std::shared_ptr<WasmModule> result) {
            EXPECT_NE(result, nullptr);
        });
    });
}

} // namespace testing
} // namespace wasm
} // namespace Zepra
