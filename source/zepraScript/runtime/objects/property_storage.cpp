// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — property_storage.cpp — Dense/sparse property backing store

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

namespace Zepra::Runtime {

enum class ElementKind : uint8_t {
    Packed,          // Dense, no holes (array[0..n-1] all present)
    Holey,           // Some indices missing
    PackedDouble,    // Dense doubles (unboxed fast path)
    HoleyDouble,
    PackedObject,    // Dense object pointers
    HoleyObject,
    Dictionary,      // Sparse (large indices or many deletions)
    Frozen,          // Sealed & frozen
};

struct PropertyDescriptor {
    uint64_t valueBits;
    uint32_t attributes;       // writable|enumerable|configurable packed in bits
    uint16_t offset;           // Offset in inline storage
    bool isAccessor;           // getter/setter vs data
    uint64_t getterBits;
    uint64_t setterBits;

    PropertyDescriptor() : valueBits(0), attributes(0x7), offset(0)
        , isAccessor(false), getterBits(0), setterBits(0) {}

    bool writable() const { return attributes & 0x1; }
    bool enumerable() const { return attributes & 0x2; }
    bool configurable() const { return attributes & 0x4; }

    void setWritable(bool v) { attributes = v ? (attributes | 0x1) : (attributes & ~0x1); }
    void setEnumerable(bool v) { attributes = v ? (attributes | 0x2) : (attributes & ~0x2); }
    void setConfigurable(bool v) { attributes = v ? (attributes | 0x4) : (attributes & ~0x4); }
};

class PropertyStorage {
public:
    static constexpr size_t kInlineSlots = 4;     // First N properties stored inline
    static constexpr size_t kDenseThreshold = 1024; // Switch to sparse above this

    PropertyStorage() : elementKind_(ElementKind::Packed), length_(0)
        , inlineCount_(0), frozen_(false), sealed_(false) {
        memset(inlineSlots_, 0, sizeof(inlineSlots_));
    }

    // Named properties.
    bool defineNamed(const std::string& name, const PropertyDescriptor& desc) {
        if (frozen_) return false;
        if (namedProps_.count(name) && sealed_ && !namedProps_[name].configurable()) {
            return false;
        }
        namedProps_[name] = desc;
        return true;
    }

    bool getNamed(const std::string& name, PropertyDescriptor& out) const {
        auto it = namedProps_.find(name);
        if (it == namedProps_.end()) return false;
        out = it->second;
        return true;
    }

    bool setNamed(const std::string& name, uint64_t value) {
        auto it = namedProps_.find(name);
        if (it == namedProps_.end()) return false;
        if (frozen_ || !it->second.writable()) return false;
        it->second.valueBits = value;
        return true;
    }

    bool deleteNamed(const std::string& name) {
        if (sealed_) return false;
        auto it = namedProps_.find(name);
        if (it == namedProps_.end()) return false;
        if (!it->second.configurable()) return false;
        namedProps_.erase(it);
        return true;
    }

    bool hasNamed(const std::string& name) const {
        return namedProps_.count(name) > 0;
    }

    // Indexed properties (elements).
    bool getElement(uint32_t index, uint64_t& out) const {
        if (elementKind_ == ElementKind::Dictionary) {
            auto it = sparseElements_.find(index);
            if (it == sparseElements_.end()) return false;
            out = it->second;
            return true;
        }
        if (index >= denseElements_.size()) return false;
        out = denseElements_[index];
        return true;
    }

    bool setElement(uint32_t index, uint64_t value) {
        if (frozen_) return false;

        if (elementKind_ == ElementKind::Dictionary) {
            sparseElements_[index] = value;
            if (index >= length_) length_ = index + 1;
            return true;
        }

        // Transition to sparse if needed.
        if (index >= kDenseThreshold && index > denseElements_.size() * 4) {
            transitionToSparse();
            sparseElements_[index] = value;
            if (index >= length_) length_ = index + 1;
            return true;
        }

        if (index >= denseElements_.size()) {
            size_t oldSize = denseElements_.size();
            denseElements_.resize(index + 1, 0);
            if (oldSize < index) {
                elementKind_ = (elementKind_ == ElementKind::Packed)
                    ? ElementKind::Holey : elementKind_;
            }
        }

        denseElements_[index] = value;
        if (index >= length_) length_ = index + 1;
        return true;
    }

    bool deleteElement(uint32_t index) {
        if (sealed_) return false;
        if (elementKind_ == ElementKind::Dictionary) {
            sparseElements_.erase(index);
            return true;
        }
        if (index < denseElements_.size()) {
            denseElements_[index] = 0;
            if (elementKind_ == ElementKind::Packed) {
                elementKind_ = ElementKind::Holey;
            }
        }
        return true;
    }

    // Array-like length.
    uint32_t length() const { return static_cast<uint32_t>(length_); }
    void setLength(uint32_t len) {
        if (len < length_ && !sealed_) {
            if (elementKind_ != ElementKind::Dictionary) {
                denseElements_.resize(len);
            }
        }
        length_ = len;
    }

    // Object freeze/seal.
    void freeze() {
        frozen_ = true;
        sealed_ = true;
        for (auto& [name, prop] : namedProps_) {
            prop.setWritable(false);
            prop.setConfigurable(false);
        }
    }

    void seal() {
        sealed_ = true;
        for (auto& [name, prop] : namedProps_) {
            prop.setConfigurable(false);
        }
    }

    bool isFrozen() const { return frozen_; }
    bool isSealed() const { return sealed_; }

    ElementKind elementKind() const { return elementKind_; }
    size_t namedPropertyCount() const { return namedProps_.size(); }

    // Enumerate own property names (named + indexed).
    std::vector<std::string> ownKeys() const {
        std::vector<std::string> keys;

        // Indices first (sorted numerically).
        if (elementKind_ == ElementKind::Dictionary) {
            std::vector<uint32_t> indices;
            for (auto& [idx, _] : sparseElements_) indices.push_back(idx);
            std::sort(indices.begin(), indices.end());
            for (auto idx : indices) keys.push_back(std::to_string(idx));
        } else {
            for (size_t i = 0; i < denseElements_.size(); i++) {
                if (denseElements_[i] != 0) keys.push_back(std::to_string(i));
            }
        }

        // Named properties.
        for (auto& [name, _] : namedProps_) {
            keys.push_back(name);
        }

        return keys;
    }

private:
    void transitionToSparse() {
        for (size_t i = 0; i < denseElements_.size(); i++) {
            if (denseElements_[i] != 0) {
                sparseElements_[static_cast<uint32_t>(i)] = denseElements_[i];
            }
        }
        denseElements_.clear();
        elementKind_ = ElementKind::Dictionary;
    }

    // Inline storage for first N properties (avoids heap allocation for small objects).
    uint64_t inlineSlots_[kInlineSlots];
    uint8_t inlineCount_;

    // Named properties.
    std::unordered_map<std::string, PropertyDescriptor> namedProps_;

    // Indexed properties.
    ElementKind elementKind_;
    std::vector<uint64_t> denseElements_;
    std::unordered_map<uint32_t, uint64_t> sparseElements_;
    size_t length_;

    bool frozen_;
    bool sealed_;
};

} // namespace Zepra::Runtime
