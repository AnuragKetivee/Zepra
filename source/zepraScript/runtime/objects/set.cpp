// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — set.cpp — ES6 Set: hash set, insertion-order, ES2025 set methods

#include "runtime/objects/set.hpp"
#include <cstring>
#include <cassert>

namespace Zepra::Runtime {

Set::Set() : Object(), head_(kEmpty), tail_(kEmpty), size_(0)
    , capacity_(kInitialCapacity) {
    buckets_.resize(capacity_, kEmpty);
    entries_.reserve(capacity_);
}

uint32_t Set::hashValue(const Value& val) const {
    if (val.isNumber()) {
        double d = val.asNumber();
        if (d == 0.0) d = 0.0;
        uint64_t bits;
        memcpy(&bits, &d, sizeof(bits));
        bits ^= bits >> 33;
        bits *= 0xff51afd7ed558ccdULL;
        bits ^= bits >> 33;
        return static_cast<uint32_t>(bits);
    }
    if (val.isString()) {
        const char* str = val.asCString();
        if (!str) return 0;
        uint32_t h = 2166136261u;
        while (*str) {
            h ^= static_cast<uint8_t>(*str++);
            h *= 16777619u;
        }
        return h;
    }
    if (val.isBool()) return val.asBool() ? 1 : 0;
    if (val.isNull()) return 0x12345678u;
    if (val.isUndefined()) return 0x87654321u;
    if (val.isSymbol()) return val.asSymbol() * 2654435761u;
    if (val.isObject()) {
        return static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(val.asObject()) >> 3) * 2654435761u;
    }
    return 0;
}

uint32_t Set::findBucket(const Value& val) const {
    if (capacity_ == 0) return kEmpty;
    uint32_t hash = hashValue(val);
    uint32_t mask = static_cast<uint32_t>(capacity_ - 1);

    for (uint32_t probe = 0; probe < static_cast<uint32_t>(capacity_); probe++) {
        uint32_t idx = (hash + probe) & mask;
        uint32_t entryIdx = buckets_[idx];
        if (entryIdx == kEmpty) return kEmpty;
        if (entryIdx == kDeleted) continue;
        if (entryIdx < entries_.size() && entries_[entryIdx].occupied
            && entries_[entryIdx].value.strictEquals(val)) {
            return idx;
        }
    }
    return kEmpty;
}

void Set::add(const Value& value) {
    if (findBucket(value) != kEmpty) return;

    if (static_cast<double>(size_ + 1) / capacity_ > kLoadFactor) {
        grow();
    }

    uint32_t entryIdx = static_cast<uint32_t>(entries_.size());
    entries_.push_back({});
    Entry& entry = entries_.back();
    entry.value = value;
    entry.occupied = true;
    entry.prev = tail_;
    entry.next = kEmpty;

    if (tail_ != kEmpty) entries_[tail_].next = entryIdx;
    tail_ = entryIdx;
    if (head_ == kEmpty) head_ = entryIdx;

    uint32_t hash = hashValue(value);
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

bool Set::has(const Value& value) const {
    return findBucket(value) != kEmpty;
}

bool Set::deleteValue(const Value& value) {
    uint32_t bucket = findBucket(value);
    if (bucket == kEmpty) return false;

    uint32_t entryIdx = buckets_[bucket];
    Entry& entry = entries_[entryIdx];

    if (entry.prev != kEmpty) entries_[entry.prev].next = entry.next;
    else head_ = entry.next;
    if (entry.next != kEmpty) entries_[entry.next].prev = entry.prev;
    else tail_ = entry.prev;

    entry.occupied = false;
    buckets_[bucket] = kDeleted;
    size_--;
    return true;
}

void Set::clear() {
    buckets_.assign(capacity_, kEmpty);
    entries_.clear();
    head_ = kEmpty;
    tail_ = kEmpty;
    size_ = 0;
}

std::vector<Value> Set::values() const {
    std::vector<Value> result;
    result.reserve(size_);
    uint32_t idx = head_;
    while (idx != kEmpty) {
        if (entries_[idx].occupied) result.push_back(entries_[idx].value);
        idx = entries_[idx].next;
    }
    return result;
}

void Set::forEach(std::function<void(const Value&)> callback) const {
    uint32_t idx = head_;
    while (idx != kEmpty) {
        if (entries_[idx].occupied) callback(entries_[idx].value);
        idx = entries_[idx].next;
    }
}

// ES2025 Set methods.

Set* Set::unionWith(const Set& other) const {
    Set* result = new Set();
    uint32_t idx = head_;
    while (idx != kEmpty) {
        if (entries_[idx].occupied) result->add(entries_[idx].value);
        idx = entries_[idx].next;
    }
    idx = other.head_;
    while (idx != kEmpty) {
        if (other.entries_[idx].occupied) result->add(other.entries_[idx].value);
        idx = other.entries_[idx].next;
    }
    return result;
}

Set* Set::intersection(const Set& other) const {
    Set* result = new Set();
    const Set* smaller = size_ <= other.size_ ? this : &other;
    const Set* larger = size_ <= other.size_ ? &other : this;

    uint32_t idx = smaller->head_;
    while (idx != kEmpty) {
        if (smaller->entries_[idx].occupied) {
            if (larger->has(smaller->entries_[idx].value)) {
                result->add(smaller->entries_[idx].value);
            }
        }
        idx = smaller->entries_[idx].next;
    }
    return result;
}

Set* Set::difference(const Set& other) const {
    Set* result = new Set();
    uint32_t idx = head_;
    while (idx != kEmpty) {
        if (entries_[idx].occupied && !other.has(entries_[idx].value)) {
            result->add(entries_[idx].value);
        }
        idx = entries_[idx].next;
    }
    return result;
}

Set* Set::symmetricDifference(const Set& other) const {
    Set* result = new Set();
    uint32_t idx = head_;
    while (idx != kEmpty) {
        if (entries_[idx].occupied && !other.has(entries_[idx].value)) {
            result->add(entries_[idx].value);
        }
        idx = entries_[idx].next;
    }
    idx = other.head_;
    while (idx != kEmpty) {
        if (other.entries_[idx].occupied && !has(other.entries_[idx].value)) {
            result->add(other.entries_[idx].value);
        }
        idx = other.entries_[idx].next;
    }
    return result;
}

bool Set::isSubsetOf(const Set& other) const {
    if (size_ > other.size_) return false;
    uint32_t idx = head_;
    while (idx != kEmpty) {
        if (entries_[idx].occupied && !other.has(entries_[idx].value)) return false;
        idx = entries_[idx].next;
    }
    return true;
}

bool Set::isSupersetOf(const Set& other) const {
    return other.isSubsetOf(*this);
}

bool Set::isDisjointFrom(const Set& other) const {
    const Set* smaller = size_ <= other.size_ ? this : &other;
    const Set* larger = size_ <= other.size_ ? &other : this;

    uint32_t idx = smaller->head_;
    while (idx != kEmpty) {
        if (smaller->entries_[idx].occupied && larger->has(smaller->entries_[idx].value)) {
            return false;
        }
        idx = smaller->entries_[idx].next;
    }
    return true;
}

void Set::grow() {
    capacity_ *= 2;
    rehash();
}

void Set::rehash() {
    buckets_.assign(capacity_, kEmpty);
    uint32_t mask = static_cast<uint32_t>(capacity_ - 1);

    for (uint32_t i = 0; i < static_cast<uint32_t>(entries_.size()); i++) {
        if (!entries_[i].occupied) continue;
        uint32_t hash = hashValue(entries_[i].value);
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
