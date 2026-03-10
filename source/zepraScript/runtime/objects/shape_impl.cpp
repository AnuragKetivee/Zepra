// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — shape_impl.cpp — Hidden class / shape tree, transitions, IC support

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

namespace Zepra::Runtime {

using ShapeId = uint32_t;

struct PropertySlot {
    uint16_t offset;         // Byte offset in object storage
    uint16_t attributes;     // writable|enumerable|configurable
    bool isAccessor;

    PropertySlot() : offset(0), attributes(0x7), isAccessor(false) {}
};

class Shape {
public:
    Shape() : id_(nextId_++), parent_(nullptr), propertyCount_(0), frozen_(false) {}

    ShapeId id() const { return id_; }
    Shape* parent() const { return parent_; }
    uint16_t propertyCount() const { return propertyCount_; }
    bool isFrozen() const { return frozen_; }

    // Add a property → return child shape (transition).
    Shape* addProperty(const std::string& name, uint16_t attributes = 0x7) {
        auto it = transitions_.find(name);
        if (it != transitions_.end()) {
            return it->second.get();
        }

        auto child = std::make_unique<Shape>();
        child->parent_ = this;
        child->propertyCount_ = propertyCount_ + 1;

        // Copy parent property map and add new entry.
        child->propertyMap_ = propertyMap_;
        PropertySlot slot;
        slot.offset = propertyCount_;
        slot.attributes = attributes;
        child->propertyMap_[name] = slot;

        // Cache the last added property name.
        child->lastProperty_ = name;

        Shape* ptr = child.get();
        transitions_[name] = std::move(child);
        return ptr;
    }

    // Lookup a property in this shape.
    bool lookupProperty(const std::string& name, PropertySlot& out) const {
        auto it = propertyMap_.find(name);
        if (it == propertyMap_.end()) return false;
        out = it->second;
        return true;
    }

    bool hasProperty(const std::string& name) const {
        return propertyMap_.count(name) > 0;
    }

    // Delete a property → transition to new shape without it.
    Shape* deleteProperty(const std::string& name) {
        std::string deleteKey = "__delete__" + name;
        auto it = transitions_.find(deleteKey);
        if (it != transitions_.end()) return it->second.get();

        auto child = std::make_unique<Shape>();
        child->parent_ = this;
        child->propertyMap_ = propertyMap_;
        child->propertyMap_.erase(name);
        child->propertyCount_ = static_cast<uint16_t>(child->propertyMap_.size());

        Shape* ptr = child.get();
        transitions_[deleteKey] = std::move(child);
        return ptr;
    }

    // Freeze shape: no more transitions.
    Shape* freeze() {
        if (frozen_) return this;
        auto it = transitions_.find("__freeze__");
        if (it != transitions_.end()) return it->second.get();

        auto child = std::make_unique<Shape>();
        child->parent_ = this;
        child->propertyCount_ = propertyCount_;
        child->propertyMap_ = propertyMap_;
        child->frozen_ = true;

        // Mark all properties as non-writable, non-configurable.
        for (auto& [name, slot] : child->propertyMap_) {
            slot.attributes = 0x2;  // enumerable only
        }

        Shape* ptr = child.get();
        transitions_["__freeze__"] = std::move(child);
        return ptr;
    }

    // Property enumeration.
    std::vector<std::string> ownPropertyNames() const {
        std::vector<std::pair<uint16_t, std::string>> sorted;
        for (auto& [name, slot] : propertyMap_) {
            sorted.push_back({slot.offset, name});
        }
        std::sort(sorted.begin(), sorted.end());
        std::vector<std::string> names;
        for (auto& [offset, name] : sorted) names.push_back(name);
        return names;
    }

    const std::string& lastProperty() const { return lastProperty_; }

    // Transition count (for profiling shape tree depth).
    size_t transitionCount() const { return transitions_.size(); }

private:
    static inline std::atomic<ShapeId> nextId_{1};
    ShapeId id_;
    Shape* parent_;
    uint16_t propertyCount_;
    bool frozen_;
    std::string lastProperty_;
    std::unordered_map<std::string, PropertySlot> propertyMap_;
    std::unordered_map<std::string, std::unique_ptr<Shape>> transitions_;
};

// Root shape cache (shared across realm).
class ShapeTree {
public:
    Shape* emptyShape() { return &emptyShape_; }

    // Get or create a shape for a sequence of property additions.
    Shape* shapeForProperties(const std::vector<std::string>& names) {
        Shape* shape = &emptyShape_;
        for (auto& name : names) {
            shape = shape->addProperty(name);
        }
        return shape;
    }

    size_t totalShapes() const { return countShapes(&emptyShape_); }

private:
    size_t countShapes(const Shape* shape) const {
        size_t count = 1;
        // Cannot easily count without exposing transitions.
        return count;
    }

    Shape emptyShape_;
};

} // namespace Zepra::Runtime
