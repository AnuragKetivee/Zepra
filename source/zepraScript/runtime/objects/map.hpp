#pragma once

// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — map.hpp — ES6 Map with hash table, insertion-order iteration

#include "config.hpp"
#include "value.hpp"
#include "object.hpp"
#include <vector>
#include <utility>
#include <functional>
#include <cstdint>

namespace Zepra::Runtime {

class Map : public Object {
public:
    Map();

    void set(const Value& key, const Value& value);
    Value get(const Value& key) const;
    bool has(const Value& key) const;
    bool deleteKey(const Value& key);
    void clear();

    size_t size() const { return size_; }

    std::vector<Value> keys() const;
    std::vector<Value> values() const;
    std::vector<std::pair<Value, Value>> entries() const;

    void forEach(std::function<void(const Value&, const Value&)> callback) const;

private:
    static constexpr size_t kInitialCapacity = 8;
    static constexpr double kLoadFactor = 0.75;
    static constexpr uint32_t kEmpty = UINT32_MAX;
    static constexpr uint32_t kDeleted = UINT32_MAX - 1;

    struct Entry {
        Value key;
        Value value;
        uint32_t next;    // Insertion-order linked list
        uint32_t prev;
        bool occupied;

        Entry() : next(kEmpty), prev(kEmpty), occupied(false) {}
    };

    std::vector<uint32_t> buckets_;   // Hash → entry index
    std::vector<Entry> entries_;
    uint32_t head_;                    // First in insertion order
    uint32_t tail_;                    // Last in insertion order
    size_t size_;
    size_t capacity_;

    uint32_t hashKey(const Value& key) const;
    uint32_t findBucket(const Value& key) const;
    void rehash();
    void grow();
};

} // namespace Zepra::Runtime
