// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — interpreter_builtins.cpp — Fast-path interpreter builtins

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <cmath>
#include <string>
#include <functional>

namespace Zepra::Interpreter {

using JsValue = uint64_t;
static constexpr JsValue kUndefined = 0x7FF8000000000001ULL;
static constexpr JsValue kNull = 0ULL;
static constexpr JsValue kTrue = 0x7FF8000000000002ULL;
static constexpr JsValue kFalse = 0x7FF8000000000003ULL;

// NaN-box tag utilities.
namespace Tags {
    static constexpr uint64_t kNaNBits    = 0x7FF8000000000000ULL;
    static constexpr uint64_t kTagMask    = 0xFFFF000000000000ULL;
    static constexpr uint64_t kObjectTag  = 0x7FFC000000000000ULL;
    static constexpr uint64_t kStringTag  = 0x7FFD000000000000ULL;

    inline bool isNumber(JsValue v) {
        return (v & kNaNBits) != kNaNBits || v == kNaNBits;
    }
    inline bool isObject(JsValue v) {
        return (v & kTagMask) == kObjectTag;
    }
    inline bool isString(JsValue v) {
        return (v & kTagMask) == kStringTag;
    }
    inline double asNumber(JsValue v) {
        double d; memcpy(&d, &v, 8); return d;
    }
    inline JsValue fromNumber(double d) {
        JsValue v; memcpy(&v, &d, 8); return v;
    }
}

class InterpreterBuiltins {
public:
    // typeof operator — fast path.
    static const char* typeOf(JsValue val) {
        if (val == kUndefined) return "undefined";
        if (val == kNull) return "object";  // typeof null === "object"
        if (val == kTrue || val == kFalse) return "boolean";
        if (Tags::isNumber(val)) return "number";
        if (Tags::isString(val)) return "string";
        if (Tags::isObject(val)) return "object";
        return "undefined";
    }

    // ToBoolean — fast path for common values.
    static bool toBooleanFast(JsValue val) {
        if (val == kFalse || val == kNull || val == kUndefined) return false;
        if (val == kTrue) return true;
        if (Tags::isNumber(val)) {
            double d = Tags::asNumber(val);
            return d != 0.0 && !std::isnan(d);
        }
        if (Tags::isString(val)) {
            // Empty string → false. Pointer check.
            return val != Tags::kStringTag;
        }
        return true;  // Objects are always truthy.
    }

    // Numeric add — fast path for two numbers.
    static JsValue addFast(JsValue a, JsValue b) {
        if (Tags::isNumber(a) && Tags::isNumber(b)) {
            return Tags::fromNumber(Tags::asNumber(a) + Tags::asNumber(b));
        }
        return kUndefined;  // Fall through to slow path.
    }

    // Numeric sub — fast path.
    static JsValue subFast(JsValue a, JsValue b) {
        if (Tags::isNumber(a) && Tags::isNumber(b)) {
            return Tags::fromNumber(Tags::asNumber(a) - Tags::asNumber(b));
        }
        return kUndefined;
    }

    // Numeric mul — fast path.
    static JsValue mulFast(JsValue a, JsValue b) {
        if (Tags::isNumber(a) && Tags::isNumber(b)) {
            return Tags::fromNumber(Tags::asNumber(a) * Tags::asNumber(b));
        }
        return kUndefined;
    }

    // Strict equality (===) — fast path.
    static JsValue strictEqualFast(JsValue a, JsValue b) {
        if (a == b) {
            // NaN !== NaN.
            if (Tags::isNumber(a) && std::isnan(Tags::asNumber(a))) return kFalse;
            return kTrue;
        }
        // -0 === +0.
        if (Tags::isNumber(a) && Tags::isNumber(b)) {
            return Tags::asNumber(a) == Tags::asNumber(b) ? kTrue : kFalse;
        }
        return kFalse;
    }

    // Less-than (<) — fast path for two numbers.
    static JsValue lessThanFast(JsValue a, JsValue b) {
        if (Tags::isNumber(a) && Tags::isNumber(b)) {
            double da = Tags::asNumber(a);
            double db = Tags::asNumber(b);
            if (std::isnan(da) || std::isnan(db)) return kUndefined;
            return da < db ? kTrue : kFalse;
        }
        return kUndefined;  // Slow path.
    }

    // Negation (-) — fast path.
    static JsValue negateFast(JsValue val) {
        if (Tags::isNumber(val)) {
            return Tags::fromNumber(-Tags::asNumber(val));
        }
        return kUndefined;
    }

    // Bitwise NOT (~) — fast path.
    static JsValue bitNotFast(JsValue val) {
        if (Tags::isNumber(val)) {
            int32_t i = static_cast<int32_t>(Tags::asNumber(val));
            return Tags::fromNumber(static_cast<double>(~i));
        }
        return kUndefined;
    }

    // Logical NOT (!) — always definitive.
    static JsValue logicalNot(JsValue val) {
        return toBooleanFast(val) ? kFalse : kTrue;
    }

    // Increment/Decrement — fast path.
    static JsValue incrementFast(JsValue val) {
        if (Tags::isNumber(val)) {
            return Tags::fromNumber(Tags::asNumber(val) + 1.0);
        }
        return kUndefined;
    }

    static JsValue decrementFast(JsValue val) {
        if (Tags::isNumber(val)) {
            return Tags::fromNumber(Tags::asNumber(val) - 1.0);
        }
        return kUndefined;
    }
};

} // namespace Zepra::Interpreter
