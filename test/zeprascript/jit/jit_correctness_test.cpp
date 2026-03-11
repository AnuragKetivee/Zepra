// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <string>

#include "jit/jit_compiler.h"
#include "interpreter/interpreter.h"
#include "runtime/execution_context.h"
#include "core/value.h"

namespace Zepra {
namespace jit {
namespace testing {

class JITCorrectnessTest : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_ = std::make_unique<runtime::ExecutionContext>();
        interpreter_ = std::make_unique<interpreter::Interpreter>(ctx_.get());
        compiler_ = std::make_unique<JITCompiler>(ctx_.get());
    }

    void TearDown() override {
        compiler_.reset();
        interpreter_.reset();
        ctx_.reset();
    }

    std::unique_ptr<runtime::ExecutionContext> ctx_;
    std::unique_ptr<interpreter::Interpreter> interpreter_;
    std::unique_ptr<JITCompiler> compiler_;

    core::Value RunInterpreter(const std::string& script) {
        return interpreter_->Execute(script);
    }

    core::Value RunJIT(const std::string& script) {
        auto compiled = compiler_->Compile(script);
        EXPECT_TRUE(compiled.IsValid());
        return compiled.Execute();
    }

    void ExpectEqualOutputs(const std::string& script) {
        core::Value int_val = RunInterpreter(script);
        core::Value jit_val = RunJIT(script);
        
        EXPECT_EQ(int_val.GetType(), jit_val.GetType());
        if (int_val.IsNumber()) {
            EXPECT_DOUBLE_EQ(int_val.AsNumber(), jit_val.AsNumber());
        } else if (int_val.IsString()) {
            EXPECT_EQ(int_val.AsString(), jit_val.AsString());
        } else if (int_val.IsBoolean()) {
            EXPECT_EQ(int_val.AsBoolean(), jit_val.AsBoolean());
        }
    }
};

TEST_F(JITCorrectnessTest, SimpleArithmetic) {
    ExpectEqualOutputs("return 5 + 3 * 2 - 1;");
}

TEST_F(JITCorrectnessTest, FloatMath) {
    ExpectEqualOutputs("return 3.14159 * 2.0;");
}

TEST_F(JITCorrectnessTest, StringConcatenation) {
    ExpectEqualOutputs("return 'hello ' + 'world';");
}

TEST_F(JITCorrectnessTest, LogicalEvaluation) {
    ExpectEqualOutputs("return (true && false) || (5 > 3);");
}

TEST_F(JITCorrectnessTest, LoopExecution) {
    ExpectEqualOutputs("let sum = 0; for(let i=0; i<10; i++) sum += i; return sum;");
}

TEST_F(JITCorrectnessTest, FunctionCall) {
    ExpectEqualOutputs("function square(x) { return x * x; } return square(4);");
}

TEST_F(JITCorrectnessTest, Recursion) {
    ExpectEqualOutputs("function fib(n) { if (n <= 1) return n; return fib(n-1) + fib(n-2); } return fib(6);");
}

TEST_F(JITCorrectnessTest, ArrayOperations) {
    ExpectEqualOutputs("let a = [1, 2, 3]; a.push(4); return a[0] + a[3];");
}

TEST_F(JITCorrectnessTest, ObjectPropertyAccess) {
    ExpectEqualOutputs("let obj = { x: 10, y: 20 }; return obj.x + obj.y;");
}

TEST_F(JITCorrectnessTest, DeoptimizationFallback) {
    std::string script = "function compute(x) { return x * 2; }; compute(5); return compute('not_a_number');";
    core::Value jit_val = RunJIT(script);
    core::Value int_val = RunInterpreter(script);
    EXPECT_EQ(jit_val.GetType(), int_val.GetType()); 
}

TEST_F(JITCorrectnessTest, ClosureCapture) {
    ExpectEqualOutputs("function makeAdder(x) { return function(y) { return x + y; }; } let add5 = makeAdder(5); return add5(10);");
}

TEST_F(JITCorrectnessTest, ExceptionUnwinding) {
    std::string script = "function foo() { throw 'error'; } try { foo(); } catch(e) { return 1; } return 0;";
    ExpectEqualOutputs(script);
}

} // namespace testing
} // namespace jit
} // namespace Zepra
