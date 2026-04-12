// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include <unordered_set>

namespace NXRender {
namespace Web {

struct BoxNode; // forward

// ==================================================================
// MutationObserver — DOM tree change observation
// ==================================================================

struct MutationRecord {
    enum class Type : uint8_t {
        Attributes, CharacterData, ChildList
    } type;

    const BoxNode* target = nullptr;
    std::string attributeName;
    std::string attributeNamespace;
    std::string oldValue;

    // For childList
    std::vector<const BoxNode*> addedNodes;
    std::vector<const BoxNode*> removedNodes;
    const BoxNode* previousSibling = nullptr;
    const BoxNode* nextSibling = nullptr;
};

struct MutationObserverInit {
    bool childList = false;
    bool attributes = false;
    bool characterData = false;
    bool subtree = false;
    bool attributeOldValue = false;
    bool characterDataOldValue = false;
    std::vector<std::string> attributeFilter;
};

using MutationCallback = std::function<void(const std::vector<MutationRecord>&)>;

class MutationObserver {
public:
    explicit MutationObserver(MutationCallback callback);
    ~MutationObserver();

    void observe(const BoxNode* target, const MutationObserverInit& options);
    void disconnect();
    std::vector<MutationRecord> takeRecords();

    // Internal — called by DOM mutation code
    void queueRecord(const MutationRecord& record);

    // Flush pending records (microtask checkpoint)
    void notify();

private:
    MutationCallback callback_;
    std::vector<MutationRecord> pendingRecords_;

    struct Observation {
        const BoxNode* target;
        MutationObserverInit options;
    };
    std::vector<Observation> observations_;

    bool matchesFilter(const MutationRecord& record, const Observation& obs) const;
};

// ==================================================================
// MutationObserver Registry — global dispatch
// ==================================================================

class MutationObserverRegistry {
public:
    static MutationObserverRegistry& instance();

    void registerObserver(MutationObserver* observer);
    void unregisterObserver(MutationObserver* observer);

    // Called by DOM mutation primitives
    void notifyAttributeChange(const BoxNode* target, const std::string& name,
                                const std::string& oldValue);
    void notifyCharacterDataChange(const BoxNode* target, const std::string& oldValue);
    void notifyChildListChange(const BoxNode* target,
                                const std::vector<const BoxNode*>& added,
                                const std::vector<const BoxNode*>& removed,
                                const BoxNode* previousSibling,
                                const BoxNode* nextSibling);

    // Flush all pending (microtask checkpoint)
    void notifyAll();

private:
    MutationObserverRegistry() = default;
    std::vector<MutationObserver*> observers_;
};

// ==================================================================
// IntersectionObserver — viewport/ancestor visibility tracking
// ==================================================================

struct IntersectionObserverEntry {
    const BoxNode* target = nullptr;
    float intersectionRatio = 0;       // 0..1
    bool isIntersecting = false;

    struct DOMRect {
        float x = 0, y = 0, width = 0, height = 0;
        float top() const { return y; }
        float left() const { return x; }
        float bottom() const { return y + height; }
        float right() const { return x + width; }
    };

    DOMRect boundingClientRect;
    DOMRect intersectionRect;
    DOMRect rootBounds;

    double time = 0; // DOMHighResTimeStamp
};

struct IntersectionObserverInit {
    const BoxNode* root = nullptr;    // null = viewport
    std::string rootMargin = "0px";   // "top right bottom left"
    std::vector<float> threshold = {0}; // intersection ratios
};

using IntersectionCallback = std::function<void(const std::vector<IntersectionObserverEntry>&)>;

class IntersectionObserver {
public:
    IntersectionObserver(IntersectionCallback callback, const IntersectionObserverInit& options);
    ~IntersectionObserver();

    void observe(const BoxNode* target);
    void unobserve(const BoxNode* target);
    void disconnect();
    std::vector<IntersectionObserverEntry> takeRecords();

    // Internal — called each frame by layout engine
    void computeIntersections(float viewportWidth, float viewportHeight,
                               float scrollX, float scrollY);

    const IntersectionObserverInit& options() const { return options_; }

private:
    IntersectionCallback callback_;
    IntersectionObserverInit options_;
    std::vector<const BoxNode*> targets_;
    std::vector<IntersectionObserverEntry> pendingEntries_;

    // Track previous intersection state for threshold crossing
    struct TargetState {
        const BoxNode* target;
        float prevRatio = -1;
        bool prevIntersecting = false;
    };
    std::vector<TargetState> targetStates_;

    // Parse rootMargin
    struct Margins { float top, right, bottom, left; };
    Margins parseRootMargin(const std::string& margin) const;

    // Check if threshold is crossed
    bool thresholdCrossed(float prevRatio, float newRatio) const;
};

// ==================================================================
// ResizeObserver — element size change observation
// ==================================================================

struct ResizeObserverEntry {
    const BoxNode* target = nullptr;

    struct ResizeObserverSize {
        float inlineSize = 0;   // width in horizontal flow
        float blockSize = 0;    // height in horizontal flow
    };

    ResizeObserverSize contentBoxSize;
    ResizeObserverSize borderBoxSize;
    ResizeObserverSize devicePixelContentBoxSize;

    IntersectionObserverEntry::DOMRect contentRect;
};

enum class ResizeObserverBoxOptions : uint8_t {
    ContentBox, BorderBox, DevicePixelContentBox
};

using ResizeCallback = std::function<void(const std::vector<ResizeObserverEntry>&)>;

class ResizeObserver {
public:
    explicit ResizeObserver(ResizeCallback callback);
    ~ResizeObserver();

    void observe(const BoxNode* target,
                  ResizeObserverBoxOptions box = ResizeObserverBoxOptions::ContentBox);
    void unobserve(const BoxNode* target);
    void disconnect();

    // Internal — called after layout
    void checkForChanges();

private:
    ResizeCallback callback_;

    struct ObservedTarget {
        const BoxNode* target;
        ResizeObserverBoxOptions box;
        float prevWidth = -1, prevHeight = -1;
    };
    std::vector<ObservedTarget> targets_;
};

// ==================================================================
// PerformanceObserver — performance monitoring
// ==================================================================

struct PerformanceEntry {
    enum class Type : uint8_t {
        Mark, Measure, Navigation, Resource, Paint,
        LargestContentfulPaint, FirstInput, LayoutShift,
        LongTask, Element, Event, Visibility
    } type;

    std::string name;
    double startTime = 0;  // DOMHighResTimeStamp (ms)
    double duration = 0;

    // LCP
    float size = 0;
    std::string url;
    const BoxNode* element = nullptr;

    // Layout shift
    float shiftScore = 0;
    bool hadRecentInput = false;

    // Long task
    std::string containerType;
    std::string containerSrc;
};

using PerformanceObserverCallback = std::function<void(const std::vector<PerformanceEntry>&)>;

class PerformanceObserver {
public:
    explicit PerformanceObserver(PerformanceObserverCallback callback);
    ~PerformanceObserver();

    void observe(const std::vector<PerformanceEntry::Type>& types, bool buffered = false);
    void disconnect();
    std::vector<PerformanceEntry> takeRecords();

private:
    friend class PerformanceTimeline;
    PerformanceObserverCallback callback_;
    std::vector<PerformanceEntry::Type> observedTypes_;
    std::vector<PerformanceEntry> pendingEntries_;
    bool buffered_ = false;
};

// ==================================================================
// Performance timeline
// ==================================================================

class PerformanceTimeline {
public:
    static PerformanceTimeline& instance();

    void addEntry(const PerformanceEntry& entry);
    void mark(const std::string& name);
    void measure(const std::string& name, const std::string& startMark,
                  const std::string& endMark = "");

    std::vector<PerformanceEntry> getEntries() const;
    std::vector<PerformanceEntry> getEntriesByType(PerformanceEntry::Type type) const;
    std::vector<PerformanceEntry> getEntriesByName(const std::string& name) const;

    void clearMarks(const std::string& name = "");
    void clearMeasures(const std::string& name = "");

    // Core Web Vitals
    double firstContentfulPaint() const { return fcp_; }
    double largestContentfulPaint() const { return lcp_; }
    double firstInputDelay() const { return fid_; }
    double cumulativeLayoutShift() const { return cls_; }
    double interactionToNextPaint() const { return inp_; }
    double timeToFirstByte() const { return ttfb_; }

    void setFCP(double t) { fcp_ = t; }
    void setLCP(double t) { lcp_ = t; }
    void setFID(double t) { fid_ = t; }
    void addLayoutShift(float score, bool hadInput);
    void recordInteraction(double duration);
    void setTTFB(double t) { ttfb_ = t; }

    // Register observer
    void registerObserver(PerformanceObserver* observer);
    void unregisterObserver(PerformanceObserver* observer);

private:
    PerformanceTimeline() = default;
    std::vector<PerformanceEntry> entries_;
    std::vector<PerformanceObserver*> observers_;

    double fcp_ = 0, lcp_ = 0, fid_ = 0, cls_ = 0, inp_ = 0, ttfb_ = 0;
    double maxInteraction_ = 0;
};

} // namespace Web
} // namespace NXRender
