// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — array_builtins.cpp — Array.prototype methods implementation

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <vector>
#include <functional>
#include <algorithm>
#include <string>

namespace Zepra::Runtime {

using JsValue = uint64_t;
static constexpr JsValue kUndefined = 0x7FF8000000000001ULL;

class ArrayBuiltins {
public:
    struct Callbacks {
        std::function<JsValue(JsValue fn, JsValue thisArg, JsValue* args, uint8_t argc)> callFn;
        std::function<JsValue(JsValue array, uint32_t index)> getElement;
        std::function<void(JsValue array, uint32_t index, JsValue value)> setElement;
        std::function<uint32_t(JsValue array)> getLength;
        std::function<void(JsValue array, uint32_t length)> setLength;
        std::function<JsValue(uint32_t initialLength)> createArray;
        std::function<bool(JsValue a, JsValue b)> strictEqual;
        std::function<double(JsValue val)> toNumber;
        std::function<std::string(JsValue val)> toString;
    };

    void setCallbacks(Callbacks cb) { cb_ = std::move(cb); }

    // Array.prototype.map
    JsValue map(JsValue array, JsValue callback, JsValue thisArg = kUndefined) {
        uint32_t len = cb_.getLength(array);
        JsValue result = cb_.createArray(len);

        for (uint32_t i = 0; i < len; i++) {
            JsValue element = cb_.getElement(array, i);
            JsValue args[3];
            args[0] = element;
            args[1] = makeIndex(i);
            args[2] = array;
            JsValue mapped = cb_.callFn(callback, thisArg, args, 3);
            cb_.setElement(result, i, mapped);
        }
        return result;
    }

    // Array.prototype.filter
    JsValue filter(JsValue array, JsValue callback, JsValue thisArg = kUndefined) {
        uint32_t len = cb_.getLength(array);
        JsValue result = cb_.createArray(0);
        uint32_t resultIdx = 0;

        for (uint32_t i = 0; i < len; i++) {
            JsValue element = cb_.getElement(array, i);
            JsValue args[3] = {element, makeIndex(i), array};
            JsValue keep = cb_.callFn(callback, thisArg, args, 3);
            if (isTruthy(keep)) {
                cb_.setElement(result, resultIdx++, element);
            }
        }
        cb_.setLength(result, resultIdx);
        return result;
    }

    // Array.prototype.reduce
    JsValue reduce(JsValue array, JsValue callback, JsValue initialValue = kUndefined) {
        uint32_t len = cb_.getLength(array);
        JsValue accumulator = initialValue;
        uint32_t startIdx = 0;

        if (accumulator == kUndefined) {
            if (len == 0) return kUndefined;  // Should throw TypeError
            accumulator = cb_.getElement(array, 0);
            startIdx = 1;
        }

        for (uint32_t i = startIdx; i < len; i++) {
            JsValue element = cb_.getElement(array, i);
            JsValue args[4] = {accumulator, element, makeIndex(i), array};
            accumulator = cb_.callFn(callback, kUndefined, args, 4);
        }
        return accumulator;
    }

    // Array.prototype.forEach
    void forEach(JsValue array, JsValue callback, JsValue thisArg = kUndefined) {
        uint32_t len = cb_.getLength(array);
        for (uint32_t i = 0; i < len; i++) {
            JsValue element = cb_.getElement(array, i);
            JsValue args[3] = {element, makeIndex(i), array};
            cb_.callFn(callback, thisArg, args, 3);
        }
    }

    // Array.prototype.find
    JsValue find(JsValue array, JsValue callback, JsValue thisArg = kUndefined) {
        uint32_t len = cb_.getLength(array);
        for (uint32_t i = 0; i < len; i++) {
            JsValue element = cb_.getElement(array, i);
            JsValue args[3] = {element, makeIndex(i), array};
            JsValue result = cb_.callFn(callback, thisArg, args, 3);
            if (isTruthy(result)) return element;
        }
        return kUndefined;
    }

    // Array.prototype.findIndex
    JsValue findIndex(JsValue array, JsValue callback, JsValue thisArg = kUndefined) {
        uint32_t len = cb_.getLength(array);
        for (uint32_t i = 0; i < len; i++) {
            JsValue element = cb_.getElement(array, i);
            JsValue args[3] = {element, makeIndex(i), array};
            JsValue result = cb_.callFn(callback, thisArg, args, 3);
            if (isTruthy(result)) return makeIndex(i);
        }
        return makeIndex(static_cast<uint32_t>(-1));
    }

    // Array.prototype.findLast (ES2023)
    JsValue findLast(JsValue array, JsValue callback, JsValue thisArg = kUndefined) {
        uint32_t len = cb_.getLength(array);
        for (uint32_t i = len; i > 0; i--) {
            JsValue element = cb_.getElement(array, i - 1);
            JsValue args[3] = {element, makeIndex(i - 1), array};
            JsValue result = cb_.callFn(callback, thisArg, args, 3);
            if (isTruthy(result)) return element;
        }
        return kUndefined;
    }

    // Array.prototype.every
    bool every(JsValue array, JsValue callback, JsValue thisArg = kUndefined) {
        uint32_t len = cb_.getLength(array);
        for (uint32_t i = 0; i < len; i++) {
            JsValue element = cb_.getElement(array, i);
            JsValue args[3] = {element, makeIndex(i), array};
            if (!isTruthy(cb_.callFn(callback, thisArg, args, 3))) return false;
        }
        return true;
    }

    // Array.prototype.some
    bool some(JsValue array, JsValue callback, JsValue thisArg = kUndefined) {
        uint32_t len = cb_.getLength(array);
        for (uint32_t i = 0; i < len; i++) {
            JsValue element = cb_.getElement(array, i);
            JsValue args[3] = {element, makeIndex(i), array};
            if (isTruthy(cb_.callFn(callback, thisArg, args, 3))) return true;
        }
        return false;
    }

    // Array.prototype.includes
    bool includes(JsValue array, JsValue searchElement, int32_t fromIndex = 0) {
        uint32_t len = cb_.getLength(array);
        uint32_t start = fromIndex < 0
            ? static_cast<uint32_t>(std::max<int32_t>(static_cast<int32_t>(len) + fromIndex, 0))
            : static_cast<uint32_t>(fromIndex);

        for (uint32_t i = start; i < len; i++) {
            if (sameValueZero(cb_.getElement(array, i), searchElement)) return true;
        }
        return false;
    }

    // Array.prototype.indexOf
    int32_t indexOf(JsValue array, JsValue searchElement, int32_t fromIndex = 0) {
        uint32_t len = cb_.getLength(array);
        uint32_t start = fromIndex < 0
            ? static_cast<uint32_t>(std::max<int32_t>(static_cast<int32_t>(len) + fromIndex, 0))
            : static_cast<uint32_t>(fromIndex);

        for (uint32_t i = start; i < len; i++) {
            if (cb_.strictEqual(cb_.getElement(array, i), searchElement)) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    // Array.prototype.flat (ES2019)
    JsValue flat(JsValue array, uint32_t depth = 1) {
        JsValue result = cb_.createArray(0);
        uint32_t idx = 0;
        flattenInto(result, array, depth, idx);
        return result;
    }

    // Array.prototype.at (ES2022)
    JsValue at(JsValue array, int32_t index) {
        uint32_t len = cb_.getLength(array);
        int32_t actualIdx = index < 0 ? static_cast<int32_t>(len) + index : index;
        if (actualIdx < 0 || static_cast<uint32_t>(actualIdx) >= len) return kUndefined;
        return cb_.getElement(array, static_cast<uint32_t>(actualIdx));
    }

    // Array.prototype.sort (TimSort-inspired)
    void sort(JsValue array, JsValue compareFn = kUndefined) {
        uint32_t len = cb_.getLength(array);
        if (len <= 1) return;

        // Extract values.
        std::vector<JsValue> values(len);
        for (uint32_t i = 0; i < len; i++) {
            values[i] = cb_.getElement(array, i);
        }

        // Sort.
        std::stable_sort(values.begin(), values.end(),
            [this, compareFn](JsValue a, JsValue b) -> bool {
                if (compareFn != kUndefined) {
                    JsValue args[2] = {a, b};
                    JsValue result = cb_.callFn(compareFn, kUndefined, args, 2);
                    return cb_.toNumber(result) < 0;
                }
                // Default: lexicographic string comparison.
                return cb_.toString(a) < cb_.toString(b);
            });

        // Write back.
        for (uint32_t i = 0; i < len; i++) {
            cb_.setElement(array, i, values[i]);
        }
    }

    // Array.prototype.splice
    JsValue splice(JsValue array, int32_t start, int32_t deleteCount,
                   JsValue* items, uint32_t itemCount) {
        uint32_t len = cb_.getLength(array);
        uint32_t actualStart = start < 0
            ? static_cast<uint32_t>(std::max<int32_t>(static_cast<int32_t>(len) + start, 0))
            : std::min<uint32_t>(static_cast<uint32_t>(start), len);
        uint32_t actualDelete = std::min<uint32_t>(
            std::max<int32_t>(deleteCount, 0), len - actualStart);

        // Create deleted array.
        JsValue deleted = cb_.createArray(actualDelete);
        for (uint32_t i = 0; i < actualDelete; i++) {
            cb_.setElement(deleted, i, cb_.getElement(array, actualStart + i));
        }

        // Shift elements.
        int32_t shift = static_cast<int32_t>(itemCount) - static_cast<int32_t>(actualDelete);
        if (shift > 0) {
            // Growing: shift right.
            for (uint32_t i = len; i > actualStart + actualDelete; i--) {
                cb_.setElement(array, i - 1 + shift, cb_.getElement(array, i - 1));
            }
        } else if (shift < 0) {
            // Shrinking: shift left.
            for (uint32_t i = actualStart + actualDelete; i < len; i++) {
                cb_.setElement(array, i + shift, cb_.getElement(array, i));
            }
        }

        // Insert new items.
        for (uint32_t i = 0; i < itemCount; i++) {
            cb_.setElement(array, actualStart + i, items[i]);
        }

        cb_.setLength(array, static_cast<uint32_t>(static_cast<int32_t>(len) + shift));
        return deleted;
    }

private:
    JsValue makeIndex(uint32_t i) const {
        JsValue v;
        double d = static_cast<double>(i);
        memcpy(&v, &d, sizeof(v));
        return v;
    }

    bool isTruthy(JsValue val) const {
        return val != 0 && val != kUndefined && val != 0x7FF8000000000003ULL;
    }

    bool sameValueZero(JsValue a, JsValue b) const {
        if (a == b) return true;
        // NaN === NaN under SameValueZero.
        double da, db;
        memcpy(&da, &a, sizeof(da));
        memcpy(&db, &b, sizeof(db));
        return da != da && db != db;  // Both NaN.
    }

    void flattenInto(JsValue target, JsValue source, uint32_t depth, uint32_t& targetIdx) {
        uint32_t len = cb_.getLength(source);
        for (uint32_t i = 0; i < len; i++) {
            JsValue element = cb_.getElement(source, i);
            if (depth > 0 && isArray(element)) {
                flattenInto(target, element, depth - 1, targetIdx);
            } else {
                cb_.setElement(target, targetIdx++, element);
            }
        }
    }

    bool isArray(JsValue val) const {
        // Simplified — actual check needs type tag.
        return false;
    }

    Callbacks cb_;
};

} // namespace Zepra::Runtime
