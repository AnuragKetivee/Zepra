#pragma once

// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — set.hpp — ES6 Set with hash table, insertion-order iteration

#include "config.hpp"
#include "value.hpp"
#include "object.hpp"
#include <vector>
#include <functional>
#include <cstdint>

namespace Zepra::Runtime {

class Set : public Object {
public:
    Set();

    void add(const Value& value);
    bool has(const Value& value) const;
    bool deleteValue(const Value& value);
    void clear();

    size_t size() const { return size_; }

    std::vector<Value> values() const;
    void forEach(std::function<void(const Value&)> callback) const;

    // ES2025 Set methods.
    Set* unionWith(const Set& other) const;
    Set* intersection(const Set& other) const;
    Set* difference(const Set& other) const;
    Set* symmetricDifference(const Set& other) const;
    bool isSubsetOf(const Set& other) const;
    bool isSupersetOf(const Set& other) const;
    bool isDisjointFrom(const Set& other) const;

private:
    static constexpr size_t kInitialCapacity = 8;
    static constexpr double kLoadFactor = 0.75;
    static constexpr uint32_t kEmpty = UINT32_MAX;
    static constexpr uint32_t kDeleted = UINT32_MAX - 1;

    struct Entry {
        Value value;
        uint32_t next;
        uint32_t prev;
        bool occupied;

        Entry() : next(kEmpty), prev(kEmpty), occupied(false) {}
    };

    std::vector<uint32_t> buckets_;
    std::vector<Entry> entries_;
    uint32_t head_;
    uint32_t tail_;
    size_t size_;
    size_t capacity_;

    uint32_t hashValue(const Value& val) const;
    uint32_t findBucket(const Value& val) const;
    void rehash();
    void grow();
};

} // namespace Zepra::Runtime
