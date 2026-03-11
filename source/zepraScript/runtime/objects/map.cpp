// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — map.cpp — ES6 Map: open-addressing hash table, insertion-order iteration

#include "runtime/objects/map.hpp"
#include <cstring>
#include <cassert>

namespace Zepra::Runtime {

Map::Map() : Object(), head_(kEmpty), tail_(kEmpty), size_(0)
    , capacity_(kInitialCapacity) {
    buckets_.resize(capacity_, kEmpty);
    entries_.reserve(capacity_);
}

uint32_t Map::hashKey(const Value& key) const {
    // Value-type-aware hash function.
    if (key.isNumber()) {
        double d = key.asNumber();
        // Normalize -0 to +0 per ES6 spec.
        if (d == 0.0) d = 0.0;
        uint64_t bits;
        memcpy(&bits, &d, sizeof(bits));
        // Mix bits.
        bits ^= bits >> 33;
        bits *= 0xff51afd7ed558ccdULL;
        bits ^= bits >> 33;
        bits *= 0xc4ceb9fe1a85ec53ULL;
        bits ^= bits >> 33;
        return static_cast<uint32_t>(bits);
    }
    if (key.isString()) {
        const char* str = key.asString()->value().c_str();
        if (!str) return 0;
        // FNV-1a.
        uint32_t h = 2166136261u;
        while (*str) {
            h ^= static_cast<uint8_t>(*str++);
            h *= 16777619u;
        }
        return h;
    }
    if (key.isBoolean()) {
        return key.asBoolean() ? 1 : 0;
    }
    if (key.isNull()) return 0x12345678u;
    if (key.isUndefined()) return 0x87654321u;
    if (key.isSymbol()) return key.asSymbol() * 2654435761u;
    if (key.isObject()) {
        return static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(key.asObject()) >> 3) * 2654435761u;
    }
    return 0;
}

uint32_t Map::findBucket(const Value& key) const {
    if (capacity_ == 0) return kEmpty;
    uint32_t hash = hashKey(key);
    uint32_t mask = static_cast<uint32_t>(capacity_ - 1);

    for (uint32_t probe = 0; probe < static_cast<uint32_t>(capacity_); probe++) {
        uint32_t idx = (hash + probe) & mask;
        uint32_t entryIdx = buckets_[idx];

        if (entryIdx == kEmpty) return kEmpty;
        if (entryIdx == kDeleted) continue;
        if (entryIdx < entries_.size() && entries_[entryIdx].occupied
            && entries_[entryIdx].key.strictEquals(key)) {
            return idx;
        }
    }
    return kEmpty;
}

void Map::set(const Value& key, const Value& value) {
    // Check if key exists — update in place.
    uint32_t bucket = findBucket(key);
    if (bucket != kEmpty) {
        uint32_t entryIdx = buckets_[bucket];
        entries_[entryIdx].value = value;
        return;
    }

    // Grow if needed.
    if (static_cast<double>(size_ + 1) / capacity_ > kLoadFactor) {
        grow();
    }

    // Insert new entry.
    uint32_t entryIdx = static_cast<uint32_t>(entries_.size());
    entries_.push_back({});
    Entry& entry = entries_.back();
    entry.key = key;
    entry.value = value;
    entry.occupied = true;
    entry.prev = tail_;
    entry.next = kEmpty;

    // Update insertion-order linked list.
    if (tail_ != kEmpty) {
        entries_[tail_].next = entryIdx;
    }
    tail_ = entryIdx;
    if (head_ == kEmpty) {
        head_ = entryIdx;
    }

    // Insert into hash table.
    uint32_t hash = hashKey(key);
    uint32_t mask = static_cast<uint32_t>(capacity_ - 1);
    for (uint32_t probe = 0; probe < static_cast<uint32_t>(capacity_); probe++) {
        uint32_t idx = (hash + probe) & mask;
        if (buckets_[idx] == kEmpty || buckets_[idx] == kDeleted) {
            buckets_[idx] = entryIdx;
            break;
        }
    }

    size_++;
}

Value Map::get(const Value& key) const {
    uint32_t bucket = findBucket(key);
    if (bucket != kEmpty) {
        return entries_[buckets_[bucket]].value;
    }
    return Value::undefined();
}

bool Map::has(const Value& key) const {
    return findBucket(key) != kEmpty;
}

bool Map::deleteKey(const Value& key) {
    uint32_t bucket = findBucket(key);
    if (bucket == kEmpty) return false;

    uint32_t entryIdx = buckets_[bucket];
    Entry& entry = entries_[entryIdx];

    // Remove from insertion-order linked list.
    if (entry.prev != kEmpty) {
        entries_[entry.prev].next = entry.next;
    } else {
        head_ = entry.next;
    }
    if (entry.next != kEmpty) {
        entries_[entry.next].prev = entry.prev;
    } else {
        tail_ = entry.prev;
    }

    entry.occupied = false;
    buckets_[bucket] = kDeleted;
    size_--;
    return true;
}

void Map::clear() {
    buckets_.assign(capacity_, kEmpty);
    entries_.clear();
    head_ = kEmpty;
    tail_ = kEmpty;
    size_ = 0;
}

std::vector<Value> Map::keys() const {
    std::vector<Value> result;
    result.reserve(size_);
    uint32_t idx = head_;
    while (idx != kEmpty) {
        if (entries_[idx].occupied) result.push_back(entries_[idx].key);
        idx = entries_[idx].next;
    }
    return result;
}

std::vector<Value> Map::values() const {
    std::vector<Value> result;
    result.reserve(size_);
    uint32_t idx = head_;
    while (idx != kEmpty) {
        if (entries_[idx].occupied) result.push_back(entries_[idx].value);
        idx = entries_[idx].next;
    }
    return result;
}

std::vector<std::pair<Value, Value>> Map::entries() const {
    std::vector<std::pair<Value, Value>> result;
    result.reserve(size_);
    uint32_t idx = head_;
    while (idx != kEmpty) {
        if (entries_[idx].occupied) {
            result.push_back({entries_[idx].key, entries_[idx].value});
        }
        idx = entries_[idx].next;
    }
    return result;
}

void Map::forEach(std::function<void(const Value&, const Value&)> callback) const {
    uint32_t idx = head_;
    while (idx != kEmpty) {
        if (entries_[idx].occupied) {
            callback(entries_[idx].value, entries_[idx].key);
        }
        idx = entries_[idx].next;
    }
}

void Map::grow() {
    capacity_ *= 2;
    rehash();
}

void Map::rehash() {
    buckets_.assign(capacity_, kEmpty);
    uint32_t mask = static_cast<uint32_t>(capacity_ - 1);

    for (uint32_t i = 0; i < static_cast<uint32_t>(entries_.size()); i++) {
        if (!entries_[i].occupied) continue;

        uint32_t hash = hashKey(entries_[i].key);
        for (uint32_t probe = 0; probe < static_cast<uint32_t>(capacity_); probe++) {
            uint32_t idx = (hash + probe) & mask;
            if (buckets_[idx] == kEmpty || buckets_[idx] == kDeleted) {
                buckets_[idx] = i;
                break;
            }
        }
    }
}

} // namespace Zepra::Runtime
