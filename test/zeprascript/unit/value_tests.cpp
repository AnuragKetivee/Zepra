// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — value_tests.cpp — Comprehensive unit tests for NaN-boxed Value types

#include <gtest/gtest.h>
#include "runtime/objects/value.hpp"
#include <cmath>
#include <limits>

using namespace Zepra::Runtime;

// =============================================================================
// Primitive Type Construction & Identity
// =============================================================================

TEST(ValueTests, Undefined) {
    Value v = Value::undefined();
    EXPECT_TRUE(v.isUndefined());
    EXPECT_FALSE(v.isNull());
    EXPECT_FALSE(v.isNumber());
    EXPECT_FALSE(v.isBoolean());
    EXPECT_FALSE(v.isObject());
    EXPECT_FALSE(v.isString());
    EXPECT_FALSE(v.isSymbol());
    EXPECT_TRUE(v.isNullOrUndefined());
    EXPECT_TRUE(v.isFalsy());
}

TEST(ValueTests, Null) {
    Value v = Value::null();
    EXPECT_TRUE(v.isNull());
    EXPECT_FALSE(v.isUndefined());
    EXPECT_FALSE(v.isNumber());
    EXPECT_TRUE(v.isNullOrUndefined());
    EXPECT_TRUE(v.isFalsy());
}

TEST(ValueTests, BooleanTrue) {
    Value t = Value::boolean(true);
    EXPECT_TRUE(t.isBoolean());
    EXPECT_TRUE(t.asBoolean());
    EXPECT_TRUE(t.isTruthy());
    EXPECT_FALSE(t.isFalsy());
    EXPECT_FALSE(t.isNumber());
    EXPECT_FALSE(t.isNull());
}

TEST(ValueTests, BooleanFalse) {
    Value f = Value::boolean(false);
    EXPECT_TRUE(f.isBoolean());
    EXPECT_FALSE(f.asBoolean());
    EXPECT_TRUE(f.isFalsy());
    EXPECT_FALSE(f.isTruthy());
}

TEST(ValueTests, DefaultConstructorIsUndefined) {
    Value v;
    EXPECT_TRUE(v.isUndefined());
}

// =============================================================================
// Number — Basic, Edge Cases, NaN-boxing
// =============================================================================

TEST(ValueTests, NumberBasic) {
    Value v = Value::number(42.0);
    EXPECT_TRUE(v.isNumber());
    EXPECT_EQ(v.asNumber(), 42.0);
    EXPECT_FALSE(v.isNull());
    EXPECT_FALSE(v.isUndefined());
    EXPECT_FALSE(v.isBoolean());
}

TEST(ValueTests, NumberZero) {
    Value v = Value::number(0.0);
    EXPECT_TRUE(v.isNumber());
    EXPECT_EQ(v.asNumber(), 0.0);
    EXPECT_TRUE(v.isFalsy());
}

TEST(ValueTests, NumberNegativeZero) {
    Value v = Value::number(-0.0);
    EXPECT_TRUE(v.isNumber());
    EXPECT_TRUE(std::signbit(v.asNumber()));
}

TEST(ValueTests, NumberInfinity) {
    Value pos = Value::number(std::numeric_limits<double>::infinity());
    Value neg = Value::number(-std::numeric_limits<double>::infinity());

    EXPECT_TRUE(pos.isNumber());
    EXPECT_TRUE(neg.isNumber());
    EXPECT_TRUE(std::isinf(pos.asNumber()));
    EXPECT_TRUE(std::isinf(neg.asNumber()));
    EXPECT_GT(pos.asNumber(), 0);
    EXPECT_LT(neg.asNumber(), 0);
}

TEST(ValueTests, NumberNaN) {
    Value v = Value::number(std::numeric_limits<double>::quiet_NaN());
    EXPECT_TRUE(v.isNumber());
    EXPECT_TRUE(std::isnan(v.asNumber()));
    EXPECT_TRUE(v.isFalsy());
}

TEST(ValueTests, NumberMaxSafeInteger) {
    double maxSafe = 9007199254740991.0;  // 2^53 - 1
    Value v = Value::number(maxSafe);
    EXPECT_TRUE(v.isNumber());
    EXPECT_EQ(v.asNumber(), maxSafe);
}

TEST(ValueTests, NumberSmallFraction) {
    Value v = Value::number(0.1 + 0.2);
    EXPECT_TRUE(v.isNumber());
    EXPECT_NEAR(v.asNumber(), 0.3, 1e-15);
}

TEST(ValueTests, NumberNegative) {
    Value v = Value::number(-273.15);
    EXPECT_TRUE(v.isNumber());
    EXPECT_EQ(v.asNumber(), -273.15);
}

// =============================================================================
// Arithmetic Operators
// =============================================================================

TEST(ValueTests, NumberArithmetic) {
    Value a = Value::number(10.0);
    Value b = Value::number(5.0);

    EXPECT_EQ(Value::add(a, b).asNumber(), 15.0);
    EXPECT_EQ(Value::subtract(a, b).asNumber(), 5.0);
    EXPECT_EQ(Value::multiply(a, b).asNumber(), 50.0);
    EXPECT_EQ(Value::divide(a, b).asNumber(), 2.0);
}

TEST(ValueTests, Modulo) {
    Value a = Value::number(10.0);
    Value b = Value::number(3.0);
    EXPECT_EQ(Value::modulo(a, b).asNumber(), 1.0);
}

TEST(ValueTests, ModuloNegative) {
    Value a = Value::number(-10.0);
    Value b = Value::number(3.0);
    EXPECT_EQ(Value::modulo(a, b).asNumber(), std::fmod(-10.0, 3.0));
}

TEST(ValueTests, Power) {
    Value base = Value::number(2.0);
    Value exp = Value::number(10.0);
    EXPECT_EQ(Value::power(base, exp).asNumber(), 1024.0);
}

TEST(ValueTests, PowerFractional) {
    Value base = Value::number(4.0);
    Value exp = Value::number(0.5);
    EXPECT_EQ(Value::power(base, exp).asNumber(), 2.0);
}

TEST(ValueTests, DivideByZero) {
    Value a = Value::number(1.0);
    Value zero = Value::number(0.0);
    Value result = Value::divide(a, zero);
    EXPECT_TRUE(std::isinf(result.asNumber()));
}

TEST(ValueTests, Negate) {
    Value pos = Value::number(42.0);
    Value neg = Value::negate(pos);
    EXPECT_EQ(neg.asNumber(), -42.0);
}

TEST(ValueTests, NegateZero) {
    Value zero = Value::number(0.0);
    Value negZero = Value::negate(zero);
    EXPECT_TRUE(std::signbit(negZero.asNumber()));
}

TEST(ValueTests, LogicalNot) {
    Value t = Value::boolean(true);
    Value f = Value::boolean(false);
    Value notT = Value::logicalNot(t);
    Value notF = Value::logicalNot(f);

    EXPECT_TRUE(notT.isFalsy());
    EXPECT_TRUE(notF.isTruthy());
}

TEST(ValueTests, LogicalNotNumber) {
    Value zero = Value::number(0.0);
    Value one = Value::number(1.0);

    EXPECT_TRUE(Value::logicalNot(zero).isTruthy());
    EXPECT_TRUE(Value::logicalNot(one).isFalsy());
}

// =============================================================================
// Bitwise Operators
// =============================================================================

TEST(ValueTests, BitwiseAnd) {
    Value a = Value::number(0xFF);
    Value b = Value::number(0x0F);
    EXPECT_EQ(Value::bitwiseAnd(a, b).asNumber(), static_cast<double>(0x0F));
}

TEST(ValueTests, BitwiseOr) {
    Value a = Value::number(0xF0);
    Value b = Value::number(0x0F);
    EXPECT_EQ(Value::bitwiseOr(a, b).asNumber(), static_cast<double>(0xFF));
}

TEST(ValueTests, BitwiseXor) {
    Value a = Value::number(0xFF);
    Value b = Value::number(0x0F);
    EXPECT_EQ(Value::bitwiseXor(a, b).asNumber(), static_cast<double>(0xF0));
}

TEST(ValueTests, BitwiseNot) {
    Value v = Value::number(0);
    Value result = Value::bitwiseNot(v);
    EXPECT_EQ(result.asNumber(), static_cast<double>(~0));
}

TEST(ValueTests, LeftShift) {
    Value v = Value::number(1.0);
    Value shift = Value::number(8.0);
    EXPECT_EQ(Value::leftShift(v, shift).asNumber(), 256.0);
}

TEST(ValueTests, RightShift) {
    Value v = Value::number(256.0);
    Value shift = Value::number(4.0);
    EXPECT_EQ(Value::rightShift(v, shift).asNumber(), 16.0);
}

// =============================================================================
// Comparison Operators
// =============================================================================

TEST(ValueTests, LessThan) {
    Value a = Value::number(1.0);
    Value b = Value::number(2.0);
    EXPECT_TRUE(Value::lessThan(a, b).isTruthy());
    EXPECT_TRUE(Value::lessThan(b, a).isFalsy());
}

TEST(ValueTests, LessEqual) {
    Value a = Value::number(5.0);
    Value b = Value::number(5.0);
    EXPECT_TRUE(Value::lessEqual(a, b).isTruthy());
}

TEST(ValueTests, GreaterThan) {
    Value a = Value::number(10.0);
    Value b = Value::number(5.0);
    EXPECT_TRUE(Value::greaterThan(a, b).isTruthy());
    EXPECT_TRUE(Value::greaterThan(b, a).isFalsy());
}

TEST(ValueTests, GreaterEqual) {
    Value a = Value::number(5.0);
    Value b = Value::number(5.0);
    EXPECT_TRUE(Value::greaterEqual(a, b).isTruthy());
}

// =============================================================================
// Strict Equality
// =============================================================================

TEST(ValueTests, StrictEquals) {
    EXPECT_TRUE(Value::number(42).strictEquals(Value::number(42)));
    EXPECT_FALSE(Value::number(42).strictEquals(Value::number(43)));
    EXPECT_TRUE(Value::boolean(true).strictEquals(Value::boolean(true)));
    EXPECT_TRUE(Value::null().strictEquals(Value::null()));
    EXPECT_TRUE(Value::undefined().strictEquals(Value::undefined()));
    EXPECT_FALSE(Value::null().strictEquals(Value::undefined()));
}

TEST(ValueTests, StrictEqualsDifferentTypes) {
    EXPECT_FALSE(Value::number(0).strictEquals(Value::boolean(false)));
    EXPECT_FALSE(Value::number(1).strictEquals(Value::boolean(true)));
    EXPECT_FALSE(Value::number(0).strictEquals(Value::null()));
    EXPECT_FALSE(Value::undefined().strictEquals(Value::number(0)));
}

TEST(ValueTests, StrictEqualsNaN) {
    Value nan1 = Value::number(std::numeric_limits<double>::quiet_NaN());
    Value nan2 = Value::number(std::numeric_limits<double>::quiet_NaN());
    // NaN !== NaN per IEEE 754
    EXPECT_FALSE(nan1.strictEquals(nan2));
}

// =============================================================================
// Symbol
// =============================================================================

TEST(ValueTests, SymbolCreation) {
    Value s = Value::symbol(42);
    EXPECT_TRUE(s.isSymbol());
    EXPECT_EQ(s.asSymbol(), 42u);
    EXPECT_FALSE(s.isNumber());
    EXPECT_FALSE(s.isString());
}

TEST(ValueTests, SymbolUniqueness) {
    Value s1 = Value::symbol(1);
    Value s2 = Value::symbol(2);
    EXPECT_NE(s1.rawBits(), s2.rawBits());
}

// =============================================================================
// Type Tagging — NaN-boxing Invariants
// =============================================================================

TEST(ValueTests, SizeIs8Bytes) {
    EXPECT_EQ(sizeof(Value), 8u);
}

TEST(ValueTests, AllTagsDistinct) {
    Value undef = Value::undefined();
    Value null = Value::null();
    Value t = Value::boolean(true);
    Value f = Value::boolean(false);
    Value num = Value::number(42.0);
    Value sym = Value::symbol(1);

    // All should have distinct raw bits.
    EXPECT_NE(undef.rawBits(), null.rawBits());
    EXPECT_NE(undef.rawBits(), t.rawBits());
    EXPECT_NE(null.rawBits(), f.rawBits());
    EXPECT_NE(num.rawBits(), sym.rawBits());
}

TEST(ValueTests, FalsyValues) {
    EXPECT_TRUE(Value::undefined().isFalsy());
    EXPECT_TRUE(Value::null().isFalsy());
    EXPECT_TRUE(Value::boolean(false).isFalsy());
    EXPECT_TRUE(Value::number(0.0).isFalsy());
    EXPECT_TRUE(Value::number(std::numeric_limits<double>::quiet_NaN()).isFalsy());
}

TEST(ValueTests, TruthyValues) {
    EXPECT_TRUE(Value::boolean(true).isTruthy());
    EXPECT_TRUE(Value::number(1.0).isTruthy());
    EXPECT_TRUE(Value::number(-1.0).isTruthy());
    EXPECT_TRUE(Value::number(0.1).isTruthy());
    EXPECT_TRUE(Value::symbol(0).isTruthy());
}
