// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_arena_list.cpp — Arena linked lists sorted by occupancy

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <mutex>
#include <functional>

namespace Zepra::Heap {

struct ArenaHeader;  // From gc_arena.cpp

class ArenaList {
public:
    ArenaList() : head_(nullptr), tail_(nullptr), count_(0), sizeClass_(0) {}

    explicit ArenaList(uint16_t sizeClass)
        : head_(nullptr), tail_(nullptr), count_(0), sizeClass_(sizeClass) {}

    void pushFront(ArenaHeader* arena) {
        assert(arena);
        setNext(arena, head_);
        setPrev(arena, nullptr);
        if (head_) setPrev(head_, arena);
        head_ = arena;
        if (!tail_) tail_ = arena;
        count_++;
    }

    void pushBack(ArenaHeader* arena) {
        assert(arena);
        setNext(arena, nullptr);
        setPrev(arena, tail_);
        if (tail_) setNext(tail_, arena);
        tail_ = arena;
        if (!head_) head_ = arena;
        count_++;
    }

    ArenaHeader* popFront() {
        if (!head_) return nullptr;
        ArenaHeader* arena = head_;
        head_ = getNext(arena);
        if (head_) setPrev(head_, nullptr);
        else tail_ = nullptr;
        setNext(arena, nullptr);
        setPrev(arena, nullptr);
        count_--;
        return arena;
    }

    void remove(ArenaHeader* arena) {
        assert(arena);
        ArenaHeader* prev = getPrev(arena);
        ArenaHeader* next = getNext(arena);

        if (prev) setNext(prev, next);
        else head_ = next;

        if (next) setPrev(next, prev);
        else tail_ = prev;

        setNext(arena, nullptr);
        setPrev(arena, nullptr);
        count_--;
    }

    bool contains(const ArenaHeader* arena) const {
        ArenaHeader* cur = head_;
        while (cur) {
            if (cur == arena) return true;
            cur = getNext(cur);
        }
        return false;
    }

    // Insert sorted by occupancy (ascending — least occupied first for allocation).
    void insertSorted(ArenaHeader* arena, std::function<double(const ArenaHeader*)> occupancy) {
        assert(arena);
        double occ = occupancy(arena);

        ArenaHeader* cur = head_;
        ArenaHeader* prev = nullptr;

        while (cur && occupancy(cur) < occ) {
            prev = cur;
            cur = getNext(cur);
        }

        setNext(arena, cur);
        setPrev(arena, prev);

        if (prev) setNext(prev, arena);
        else head_ = arena;

        if (cur) setPrev(cur, arena);
        else tail_ = arena;

        count_++;
    }

    // Find first arena with free cells.
    ArenaHeader* findWithFreeSpace() const {
        ArenaHeader* cur = head_;
        while (cur) {
            if (!isFull(cur)) return cur;
            cur = getNext(cur);
        }
        return nullptr;
    }

    // Partition into two lists: below threshold occupancy and above.
    void partition(double threshold, ArenaList& below, ArenaList& above,
                   std::function<double(const ArenaHeader*)> occupancy) {
        ArenaHeader* cur = head_;
        while (cur) {
            ArenaHeader* next = getNext(cur);
            setNext(cur, nullptr);
            setPrev(cur, nullptr);

            if (occupancy(cur) < threshold) {
                below.pushBack(cur);
            } else {
                above.pushBack(cur);
            }
            cur = next;
        }
        head_ = nullptr;
        tail_ = nullptr;
        count_ = 0;
    }

    template<typename Fn>
    void forEach(Fn&& fn) {
        ArenaHeader* cur = head_;
        while (cur) {
            ArenaHeader* next = getNext(cur);
            fn(cur);
            cur = next;
        }
    }

    template<typename Fn>
    void forEachReverse(Fn&& fn) {
        ArenaHeader* cur = tail_;
        while (cur) {
            ArenaHeader* prev = getPrev(cur);
            fn(cur);
            cur = prev;
        }
    }

    // Sweep all arenas, returning total freed bytes.
    size_t sweepAll(std::function<size_t(ArenaHeader*)> sweepFn) {
        size_t totalFreed = 0;
        forEach([&](ArenaHeader* arena) {
            totalFreed += sweepFn(arena);
        });
        return totalFreed;
    }

    // Remove and collect empty arenas after sweep.
    size_t removeEmpty(std::function<bool(const ArenaHeader*)> isEmpty,
                       std::function<void(ArenaHeader*)> onRemoved) {
        size_t removed = 0;
        ArenaHeader* cur = head_;
        while (cur) {
            ArenaHeader* next = getNext(cur);
            if (isEmpty(cur)) {
                remove(cur);
                if (onRemoved) onRemoved(cur);
                removed++;
            }
            cur = next;
        }
        return removed;
    }

    ArenaHeader* head() const { return head_; }
    ArenaHeader* tail() const { return tail_; }
    size_t count() const { return count_; }
    uint16_t sizeClass() const { return sizeClass_; }
    bool empty() const { return count_ == 0; }

    void clear() {
        head_ = nullptr;
        tail_ = nullptr;
        count_ = 0;
    }

private:
    // These access the next/prev pointers in ArenaHeader.
    // ArenaHeader has `next` and `prev` fields.
    static ArenaHeader* getNext(const ArenaHeader* a);
    static ArenaHeader* getPrev(const ArenaHeader* a);
    static void setNext(ArenaHeader* a, ArenaHeader* next);
    static void setPrev(ArenaHeader* a, ArenaHeader* prev);
    static bool isFull(const ArenaHeader* a);

    ArenaHeader* head_;
    ArenaHeader* tail_;
    size_t count_;
    uint16_t sizeClass_;
};

// Multi-list: one ArenaList per size class.
class ArenaListMap {
public:
    static constexpr size_t kSizeClassCount = 9;
    static constexpr uint16_t kSizeClasses[] = {16, 32, 48, 64, 128, 256, 512, 1024, 2048};

    ArenaListMap() {
        for (size_t i = 0; i < kSizeClassCount; i++) {
            lists_[i] = ArenaList(kSizeClasses[i]);
        }
    }

    ArenaList* listFor(uint16_t sizeClass) {
        for (size_t i = 0; i < kSizeClassCount; i++) {
            if (kSizeClasses[i] >= sizeClass) return &lists_[i];
        }
        return &lists_[kSizeClassCount - 1];
    }

    const ArenaList* listFor(uint16_t sizeClass) const {
        for (size_t i = 0; i < kSizeClassCount; i++) {
            if (kSizeClasses[i] >= sizeClass) return &lists_[i];
        }
        return &lists_[kSizeClassCount - 1];
    }

    size_t totalArenaCount() const {
        size_t total = 0;
        for (size_t i = 0; i < kSizeClassCount; i++) total += lists_[i].count();
        return total;
    }

    template<typename Fn>
    void forEachList(Fn&& fn) {
        for (size_t i = 0; i < kSizeClassCount; i++) fn(&lists_[i]);
    }

private:
    ArenaList lists_[kSizeClassCount];
};

} // namespace Zepra::Heap
