// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — object_layout.cpp — Object memory layout, inline vs out-of-line props

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>

namespace Zepra::Runtime {

static constexpr size_t kObjectHeaderSize = 16;     // ShapePtr + Flags + ElementsPtr
static constexpr size_t kMaxInlineProps = 6;         // Inline property slots
static constexpr size_t kSlotSize = 8;               // 64-bit value slots

struct ObjectHeader {
    void* shape;          // Pointer to Shape (hidden class)
    uint32_t flags;       // Object flag bits
    uint32_t elementCount; // Number of indexed elements

    static constexpr uint32_t kFlagExotic       = 1 << 0;
    static constexpr uint32_t kFlagCallable     = 1 << 1;
    static constexpr uint32_t kFlagConstructor  = 1 << 2;
    static constexpr uint32_t kFlagFrozen       = 1 << 3;
    static constexpr uint32_t kFlagSealed       = 1 << 4;
    static constexpr uint32_t kFlagExtensible   = 1 << 5;
    static constexpr uint32_t kFlagProxy        = 1 << 6;
    static constexpr uint32_t kFlagHasPrototype = 1 << 7;

    ObjectHeader() : shape(nullptr), flags(kFlagExtensible), elementCount(0) {}

    bool isCallable() const { return flags & kFlagCallable; }
    bool isConstructor() const { return flags & kFlagConstructor; }
    bool isFrozen() const { return flags & kFlagFrozen; }
    bool isSealed() const { return flags & kFlagSealed; }
    bool isExtensible() const { return flags & kFlagExtensible; }
    bool isProxy() const { return flags & kFlagProxy; }
};

// Object memory layout manager.
class ObjectLayout {
public:
    ObjectLayout() : outOfLineCount_(0) {}

    // Calculate total object size.
    static size_t objectSize(uint16_t propertyCount) {
        size_t inlineBytes = std::min<size_t>(propertyCount, kMaxInlineProps) * kSlotSize;
        return kObjectHeaderSize + inlineBytes;
    }

    // Determine if a property at given offset is inline or out-of-line.
    static bool isInline(uint16_t offset) {
        return offset < kMaxInlineProps;
    }

    // Get pointer to inline slot.
    static uint64_t* inlineSlot(void* objectBase, uint16_t offset) {
        assert(offset < kMaxInlineProps);
        uint8_t* base = static_cast<uint8_t*>(objectBase) + kObjectHeaderSize;
        return reinterpret_cast<uint64_t*>(base + offset * kSlotSize);
    }

    // Out-of-line storage management.
    void ensureOutOfLineCapacity(uint16_t needed) {
        if (needed <= outOfLineSlots_.size()) return;
        outOfLineSlots_.resize(needed, 0);
    }

    uint64_t getOutOfLine(uint16_t offset) const {
        uint16_t oofIdx = offset - kMaxInlineProps;
        return oofIdx < outOfLineSlots_.size() ? outOfLineSlots_[oofIdx] : 0;
    }

    void setOutOfLine(uint16_t offset, uint64_t value) {
        uint16_t oofIdx = offset - kMaxInlineProps;
        if (oofIdx >= outOfLineSlots_.size()) {
            outOfLineSlots_.resize(oofIdx + 1, 0);
        }
        outOfLineSlots_[oofIdx] = value;
    }

    // Read a property value given its offset.
    uint64_t readProperty(void* objectBase, uint16_t offset) const {
        if (isInline(offset)) {
            return *inlineSlot(objectBase, offset);
        }
        return getOutOfLine(offset);
    }

    // Write a property value.
    void writeProperty(void* objectBase, uint16_t offset, uint64_t value) {
        if (isInline(offset)) {
            *inlineSlot(objectBase, offset) = value;
        } else {
            setOutOfLine(offset, value);
        }
    }

    // Prototype chain walking.
    struct PrototypeChainEntry {
        void* object;
        void* shape;
    };

    static std::vector<PrototypeChainEntry> walkPrototypeChain(void* object,
        std::function<void*(void* obj)> getPrototype,
        size_t maxDepth = 256) {
        std::vector<PrototypeChainEntry> chain;
        void* current = object;
        size_t depth = 0;

        while (current && depth < maxDepth) {
            ObjectHeader* header = static_cast<ObjectHeader*>(current);
            chain.push_back({current, header->shape});
            current = getPrototype(current);
            depth++;
        }

        return chain;
    }

    // Calculate slack (unused pre-allocated inline slots).
    static size_t calculateSlack(uint16_t propertyCount) {
        if (propertyCount >= kMaxInlineProps) return 0;
        return kMaxInlineProps - propertyCount;
    }

    size_t outOfLineCount() const { return outOfLineSlots_.size(); }

private:
    std::vector<uint64_t> outOfLineSlots_;
    size_t outOfLineCount_;
};

} // namespace Zepra::Runtime
