// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "observers.h"
#include "box/box_tree.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <sstream>
#include <cstring>

namespace NXRender {
namespace Web {

// ==================================================================
// MutationObserver
// ==================================================================

MutationObserver::MutationObserver(MutationCallback callback)
    : callback_(std::move(callback)) {
    MutationObserverRegistry::instance().registerObserver(this);
}

MutationObserver::~MutationObserver() {
    MutationObserverRegistry::instance().unregisterObserver(this);
}

void MutationObserver::observe(const BoxNode* target, const MutationObserverInit& options) {
    // Remove existing observation for this target
    observations_.erase(
        std::remove_if(observations_.begin(), observations_.end(),
                        [target](const Observation& o) { return o.target == target; }),
        observations_.end());

    observations_.push_back({target, options});
}

void MutationObserver::disconnect() {
    observations_.clear();
    pendingRecords_.clear();
}

std::vector<MutationRecord> MutationObserver::takeRecords() {
    std::vector<MutationRecord> records;
    records.swap(pendingRecords_);
    return records;
}

void MutationObserver::queueRecord(const MutationRecord& record) {
    for (const auto& obs : observations_) {
        if (matchesFilter(record, obs)) {
            pendingRecords_.push_back(record);
            return;
        }
    }
}

void MutationObserver::notify() {
    if (pendingRecords_.empty()) return;
    auto records = takeRecords();
    if (callback_) callback_(records);
}

bool MutationObserver::matchesFilter(const MutationRecord& record,
                                       const Observation& obs) const {
    // Check target match
    bool targetMatch = (record.target == obs.target);
    if (!targetMatch && obs.options.subtree) {
        // Walk up parent chain
        const BoxNode* node = record.target;
        while (node) {
            if (node == obs.target) { targetMatch = true; break; }
            node = node->parent();
        }
    }
    if (!targetMatch) return false;

    // Check type match
    switch (record.type) {
        case MutationRecord::Type::ChildList:
            return obs.options.childList;
        case MutationRecord::Type::Attributes:
            if (!obs.options.attributes) return false;
            if (!obs.options.attributeFilter.empty()) {
                for (const auto& f : obs.options.attributeFilter) {
                    if (f == record.attributeName) return true;
                }
                return false;
            }
            return true;
        case MutationRecord::Type::CharacterData:
            return obs.options.characterData;
    }
    return false;
}

// ==================================================================
// MutationObserverRegistry
// ==================================================================

MutationObserverRegistry& MutationObserverRegistry::instance() {
    static MutationObserverRegistry inst;
    return inst;
}

void MutationObserverRegistry::registerObserver(MutationObserver* observer) {
    observers_.push_back(observer);
}

void MutationObserverRegistry::unregisterObserver(MutationObserver* observer) {
    observers_.erase(
        std::remove(observers_.begin(), observers_.end(), observer),
        observers_.end());
}

void MutationObserverRegistry::notifyAttributeChange(const BoxNode* target,
                                                        const std::string& name,
                                                        const std::string& oldValue) {
    MutationRecord record;
    record.type = MutationRecord::Type::Attributes;
    record.target = target;
    record.attributeName = name;
    record.oldValue = oldValue;
    for (auto* obs : observers_) obs->queueRecord(record);
}

void MutationObserverRegistry::notifyCharacterDataChange(const BoxNode* target,
                                                            const std::string& oldValue) {
    MutationRecord record;
    record.type = MutationRecord::Type::CharacterData;
    record.target = target;
    record.oldValue = oldValue;
    for (auto* obs : observers_) obs->queueRecord(record);
}

void MutationObserverRegistry::notifyChildListChange(
    const BoxNode* target,
    const std::vector<const BoxNode*>& added,
    const std::vector<const BoxNode*>& removed,
    const BoxNode* previousSibling,
    const BoxNode* nextSibling) {

    MutationRecord record;
    record.type = MutationRecord::Type::ChildList;
    record.target = target;
    record.addedNodes = added;
    record.removedNodes = removed;
    record.previousSibling = previousSibling;
    record.nextSibling = nextSibling;
    for (auto* obs : observers_) obs->queueRecord(record);
}

void MutationObserverRegistry::notifyAll() {
    // Copy to avoid re-entrancy issues
    auto observers = observers_;
    for (auto* obs : observers) obs->notify();
}

// ==================================================================
// IntersectionObserver
// ==================================================================

IntersectionObserver::IntersectionObserver(IntersectionCallback callback,
                                             const IntersectionObserverInit& options)
    : callback_(std::move(callback)), options_(options) {}

IntersectionObserver::~IntersectionObserver() {
    disconnect();
}

void IntersectionObserver::observe(const BoxNode* target) {
    // Check not already observing
    for (const auto* t : targets_) {
        if (t == target) return;
    }
    targets_.push_back(target);
    targetStates_.push_back({target, -1, false});
}

void IntersectionObserver::unobserve(const BoxNode* target) {
    targets_.erase(std::remove(targets_.begin(), targets_.end(), target), targets_.end());
    targetStates_.erase(
        std::remove_if(targetStates_.begin(), targetStates_.end(),
                        [target](const TargetState& s) { return s.target == target; }),
        targetStates_.end());
}

void IntersectionObserver::disconnect() {
    targets_.clear();
    targetStates_.clear();
    pendingEntries_.clear();
}

std::vector<IntersectionObserverEntry> IntersectionObserver::takeRecords() {
    std::vector<IntersectionObserverEntry> entries;
    entries.swap(pendingEntries_);
    return entries;
}

IntersectionObserver::Margins IntersectionObserver::parseRootMargin(
    const std::string& margin) const {
    Margins m = {0, 0, 0, 0};
    std::istringstream ss(margin);
    std::vector<float> values;
    std::string token;
    while (ss >> token) {
        values.push_back(std::strtof(token.c_str(), nullptr));
    }
    if (values.size() >= 1) m.top = m.right = m.bottom = m.left = values[0];
    if (values.size() >= 2) { m.right = m.left = values[1]; }
    if (values.size() >= 3) { m.bottom = values[2]; }
    if (values.size() >= 4) { m.left = values[3]; }
    return m;
}

bool IntersectionObserver::thresholdCrossed(float prevRatio, float newRatio) const {
    for (float t : options_.threshold) {
        bool prevAbove = (prevRatio >= t);
        bool newAbove = (newRatio >= t);
        if (prevAbove != newAbove) return true;
    }
    return false;
}

void IntersectionObserver::computeIntersections(float viewportWidth, float viewportHeight,
                                                   float scrollX, float scrollY) {
    Margins margin = parseRootMargin(options_.rootMargin);

    // Root bounds (viewport or custom root)
    IntersectionObserverEntry::DOMRect rootRect;
    if (options_.root) {
        auto& rl = options_.root->layoutBox();
        rootRect = {rl.x, rl.y, rl.width, rl.height};
    } else {
        rootRect = {scrollX - margin.left, scrollY - margin.top,
                     viewportWidth + margin.left + margin.right,
                     viewportHeight + margin.top + margin.bottom};
    }

    std::vector<IntersectionObserverEntry> newEntries;

    for (size_t i = 0; i < targets_.size(); i++) {
        const BoxNode* target = targets_[i];
        if (!target) continue;

        auto& tl = target->layoutBox();
        IntersectionObserverEntry::DOMRect targetRect = {
            tl.x, tl.y, tl.width, tl.height
        };

        // Compute intersection
        float interLeft = std::max(rootRect.x, targetRect.x);
        float interTop = std::max(rootRect.y, targetRect.y);
        float interRight = std::min(rootRect.right(), targetRect.right());
        float interBottom = std::min(rootRect.bottom(), targetRect.bottom());

        IntersectionObserverEntry::DOMRect intersectionRect = {0, 0, 0, 0};
        float ratio = 0;
        bool isIntersecting = false;

        if (interRight > interLeft && interBottom > interTop) {
            intersectionRect = {interLeft, interTop,
                                 interRight - interLeft, interBottom - interTop};
            float targetArea = targetRect.width * targetRect.height;
            if (targetArea > 0) {
                float interArea = intersectionRect.width * intersectionRect.height;
                ratio = interArea / targetArea;
            }
            isIntersecting = true;
        }

        // Check threshold crossing
        float prevRatio = (i < targetStates_.size()) ? targetStates_[i].prevRatio : -1;
        if (prevRatio < 0 || thresholdCrossed(prevRatio, ratio)) {
            auto now = std::chrono::steady_clock::now();
            double timestamp = std::chrono::duration<double, std::milli>(
                now.time_since_epoch()).count();

            IntersectionObserverEntry entry;
            entry.target = target;
            entry.intersectionRatio = ratio;
            entry.isIntersecting = isIntersecting;
            entry.boundingClientRect = targetRect;
            entry.intersectionRect = intersectionRect;
            entry.rootBounds = rootRect;
            entry.time = timestamp;
            newEntries.push_back(entry);
        }

        if (i < targetStates_.size()) {
            targetStates_[i].prevRatio = ratio;
            targetStates_[i].prevIntersecting = isIntersecting;
        }
    }

    if (!newEntries.empty()) {
        if (callback_) callback_(newEntries);
    }
}

// ==================================================================
// ResizeObserver
// ==================================================================

ResizeObserver::ResizeObserver(ResizeCallback callback)
    : callback_(std::move(callback)) {}

ResizeObserver::~ResizeObserver() {
    disconnect();
}

void ResizeObserver::observe(const BoxNode* target, ResizeObserverBoxOptions box) {
    for (const auto& t : targets_) {
        if (t.target == target) return;
    }
    targets_.push_back({target, box, -1, -1});
}

void ResizeObserver::unobserve(const BoxNode* target) {
    targets_.erase(
        std::remove_if(targets_.begin(), targets_.end(),
                        [target](const ObservedTarget& t) { return t.target == target; }),
        targets_.end());
}

void ResizeObserver::disconnect() {
    targets_.clear();
}

void ResizeObserver::checkForChanges() {
    std::vector<ResizeObserverEntry> entries;

    for (auto& t : targets_) {
        if (!t.target) continue;

        auto& lb = t.target->layoutBox();
        float currentWidth = 0, currentHeight = 0;
        switch (t.box) {
            case ResizeObserverBoxOptions::ContentBox:
                currentWidth = lb.contentWidth;
                currentHeight = lb.contentHeight;
                break;
            case ResizeObserverBoxOptions::BorderBox:
                currentWidth = lb.width;
                currentHeight = lb.height;
                break;
            case ResizeObserverBoxOptions::DevicePixelContentBox:
                currentWidth = lb.contentWidth;
                currentHeight = lb.contentHeight;
                break;
        }

        if (currentWidth != t.prevWidth || currentHeight != t.prevHeight) {
            ResizeObserverEntry entry;
            entry.target = t.target;

            entry.contentBoxSize = {lb.contentWidth, lb.contentHeight};
            entry.borderBoxSize = {lb.width, lb.height};
            entry.devicePixelContentBoxSize = {lb.contentWidth, lb.contentHeight};
            entry.contentRect = {lb.paddingLeft, lb.paddingTop,
                                  lb.contentWidth, lb.contentHeight};

            entries.push_back(entry);
            t.prevWidth = currentWidth;
            t.prevHeight = currentHeight;
        }
    }

    if (!entries.empty() && callback_) {
        callback_(entries);
    }
}

// ==================================================================
// PerformanceObserver
// ==================================================================

PerformanceObserver::PerformanceObserver(PerformanceObserverCallback callback)
    : callback_(std::move(callback)) {
    PerformanceTimeline::instance().registerObserver(this);
}

PerformanceObserver::~PerformanceObserver() {
    PerformanceTimeline::instance().unregisterObserver(this);
}

void PerformanceObserver::observe(const std::vector<PerformanceEntry::Type>& types,
                                    bool buffered) {
    observedTypes_ = types;
    buffered_ = buffered;

    if (buffered) {
        auto& timeline = PerformanceTimeline::instance();
        for (auto type : types) {
            auto entries = timeline.getEntriesByType(type);
            pendingEntries_.insert(pendingEntries_.end(), entries.begin(), entries.end());
        }
        if (!pendingEntries_.empty() && callback_) {
            auto records = takeRecords();
            callback_(records);
        }
    }
}

void PerformanceObserver::disconnect() {
    observedTypes_.clear();
    pendingEntries_.clear();
}

std::vector<PerformanceEntry> PerformanceObserver::takeRecords() {
    std::vector<PerformanceEntry> entries;
    entries.swap(pendingEntries_);
    return entries;
}

// ==================================================================
// PerformanceTimeline
// ==================================================================

PerformanceTimeline& PerformanceTimeline::instance() {
    static PerformanceTimeline inst;
    return inst;
}

void PerformanceTimeline::addEntry(const PerformanceEntry& entry) {
    entries_.push_back(entry);

    // Notify observers
    for (auto* obs : observers_) {
        for (auto type : obs->observedTypes_) {
            if (type == entry.type) {
                obs->pendingEntries_.push_back(entry);
                auto records = obs->takeRecords();
                if (obs->callback_) obs->callback_(records);
                break;
            }
        }
    }
}

void PerformanceTimeline::mark(const std::string& name) {
    auto now = std::chrono::steady_clock::now();
    double timestamp = std::chrono::duration<double, std::milli>(
        now.time_since_epoch()).count();

    PerformanceEntry entry;
    entry.type = PerformanceEntry::Type::Mark;
    entry.name = name;
    entry.startTime = timestamp;
    addEntry(entry);
}

void PerformanceTimeline::measure(const std::string& name, const std::string& startMark,
                                    const std::string& endMark) {
    double startTime = 0, endTime = 0;

    for (const auto& e : entries_) {
        if (e.type == PerformanceEntry::Type::Mark) {
            if (e.name == startMark) startTime = e.startTime;
            if (e.name == endMark) endTime = e.startTime;
        }
    }

    if (endMark.empty()) {
        auto now = std::chrono::steady_clock::now();
        endTime = std::chrono::duration<double, std::milli>(
            now.time_since_epoch()).count();
    }

    PerformanceEntry entry;
    entry.type = PerformanceEntry::Type::Measure;
    entry.name = name;
    entry.startTime = startTime;
    entry.duration = endTime - startTime;
    addEntry(entry);
}

std::vector<PerformanceEntry> PerformanceTimeline::getEntries() const {
    return entries_;
}

std::vector<PerformanceEntry> PerformanceTimeline::getEntriesByType(
    PerformanceEntry::Type type) const {
    std::vector<PerformanceEntry> result;
    for (const auto& e : entries_) {
        if (e.type == type) result.push_back(e);
    }
    return result;
}

std::vector<PerformanceEntry> PerformanceTimeline::getEntriesByName(
    const std::string& name) const {
    std::vector<PerformanceEntry> result;
    for (const auto& e : entries_) {
        if (e.name == name) result.push_back(e);
    }
    return result;
}

void PerformanceTimeline::clearMarks(const std::string& name) {
    if (name.empty()) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [](const PerformanceEntry& e) { return e.type == PerformanceEntry::Type::Mark; }),
            entries_.end());
    } else {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [&name](const PerformanceEntry& e) {
                return e.type == PerformanceEntry::Type::Mark && e.name == name;
            }), entries_.end());
    }
}

void PerformanceTimeline::clearMeasures(const std::string& name) {
    if (name.empty()) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [](const PerformanceEntry& e) { return e.type == PerformanceEntry::Type::Measure; }),
            entries_.end());
    } else {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [&name](const PerformanceEntry& e) {
                return e.type == PerformanceEntry::Type::Measure && e.name == name;
            }), entries_.end());
    }
}

void PerformanceTimeline::addLayoutShift(float score, bool hadInput) {
    cls_ += score;
    PerformanceEntry entry;
    entry.type = PerformanceEntry::Type::LayoutShift;
    entry.shiftScore = score;
    entry.hadRecentInput = hadInput;
    auto now = std::chrono::steady_clock::now();
    entry.startTime = std::chrono::duration<double, std::milli>(
        now.time_since_epoch()).count();
    addEntry(entry);
}

void PerformanceTimeline::recordInteraction(double duration) {
    if (duration > maxInteraction_) {
        maxInteraction_ = duration;
        inp_ = duration;
    }
}

void PerformanceTimeline::registerObserver(PerformanceObserver* observer) {
    observers_.push_back(observer);
}

void PerformanceTimeline::unregisterObserver(PerformanceObserver* observer) {
    observers_.erase(std::remove(observers_.begin(), observers_.end(), observer),
                      observers_.end());
}

} // namespace Web
} // namespace NXRender
