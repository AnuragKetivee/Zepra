// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include <gtest/gtest.h>
#include <string>
#include <memory>
#include <limits>

#include "interpreter/interpreter.h"
#include "runtime/execution_context.h"
#include "runtime/objects/CoercionAPI.h"
#include "core/value.h"

namespace Zepra {
namespace interpreter {
namespace testing {

class InterpreterEdgeCasesTest : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_ = std::make_unique<runtime::ExecutionContext>();
        interpreter_ = std::make_unique<Interpreter>(ctx_.get());
    }

    void TearDown() override {
        interpreter_.reset();
        ctx_.reset();
    }

    std::unique_ptr<runtime::ExecutionContext> ctx_;
    std::unique_ptr<Interpreter> interpreter_;

    core::Value Execute(const std::string& script) {
        return interpreter_->Execute(script);
    }
    
    void ExpectError(const std::string& script, const std::string& error_type) {
        core::Value result = interpreter_->Execute(script);
        EXPECT_TRUE(result.IsException());
        EXPECT_EQ(result.GetExceptionType(), error_type);
    }
};

TEST_F(InterpreterEdgeCasesTest, NumberOverflowToInfinity) {
    core::Value result = Execute("return Number.MAX_VALUE * 2;");
    EXPECT_TRUE(result.IsNumber());
    EXPECT_TRUE(result.IsInfinity());
}

TEST_F(InterpreterEdgeCasesTest, DivisionByZeroGivesInfinity) {
    core::Value result = Execute("return 1 / 0;");
    EXPECT_TRUE(result.IsNumber());
    EXPECT_TRUE(result.IsInfinity());
    
    core::Value neg_result = Execute("return -1 / 0;");
    EXPECT_TRUE(neg_result.IsNumber());
    EXPECT_TRUE(neg_result.IsNegativeInfinity());
}

TEST_F(InterpreterEdgeCasesTest, ZeroDividedByZeroGivesNaN) {
    core::Value result = Execute("return 0 / 0;");
    EXPECT_TRUE(result.IsNumber());
    EXPECT_TRUE(result.IsNaN());
}

TEST_F(InterpreterEdgeCasesTest, DeeplyNestedObjectLiteral) {
    std::string script = "let obj = ";
    for(int i=0; i<100; i++) script += "{ a: ";
    script += "123";
    for(int i=0; i<100; i++) script += " }";
    script += "; return obj";
    for(int i=0; i<100; i++) script += ".a";
    script += ";";
    
    core::Value result = Execute(script);
    EXPECT_EQ(result.AsNumber(), 123);
}

TEST_F(InterpreterEdgeCasesTest, StringCoercionWithObjects) {
    core::Value result = Execute("return 'Base' + {};");
    EXPECT_EQ(result.AsString(), "Base[object Object]");
}

TEST_F(InterpreterEdgeCasesTest, BooleanCoercion) {
    core::Value result = Execute("return !![] && !!{} && !'' && !0 && !null && !undefined;");
    EXPECT_TRUE(result.AsBoolean());
}

TEST_F(InterpreterEdgeCasesTest, LargeArrayHoleHandling) {
    core::Value result = Execute("let a = []; a[1000] = 5; return a.length;");
    EXPECT_EQ(result.AsNumber(), 1001);
}

TEST_F(InterpreterEdgeCasesTest, BitwiseOperationsSignExtension) {
    core::Value result = Execute("return ~0;");
    EXPECT_EQ(result.AsNumber(), -1);
}

TEST_F(InterpreterEdgeCasesTest, OutOfBoundsStringCharAccess) {
    core::Value result = Execute("let s = 'test'; return s[10];");
    EXPECT_TRUE(result.IsUndefined());
}

TEST_F(InterpreterEdgeCasesTest, ProtoAssignmentCyclic) {
    ExpectError("let a = {}; let b = {}; a.__proto__ = b; b.__proto__ = a;", "TypeError");
}

TEST_F(InterpreterEdgeCasesTest, UndefinedPlusNumberIsNaN) {
    core::Value result = Execute("return undefined + 5;");
    EXPECT_TRUE(result.IsNaN());
}

TEST_F(InterpreterEdgeCasesTest, ThrowingInGetterRejectsOperation) {
    std::string script = "let obj = { get x() { throw 'Cannot read'; } }; try { let y = obj.x; return 0; } catch(e) { return 1; }";
    core::Value result = Execute(script);
    EXPECT_EQ(result.AsNumber(), 1);
}

TEST_F(InterpreterEdgeCasesTest, ArraySortWithUndefinedsAndNulls) {
    core::Value result = Execute("let a = [null, undefined, 2, 1]; a.sort(); return a[0] === 1 && a[1] === 2 && a[2] === null && a[3] === undefined;");
    EXPECT_TRUE(result.AsBoolean());
}

} // namespace testing
} // namespace interpreter
} // namespace Zepra
