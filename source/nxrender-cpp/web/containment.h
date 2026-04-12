// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <mutex>

namespace NXRender {

// ==================================================================
// CSS Containment — contain: size/layout/paint/style/content
// ==================================================================

enum class ContainFlag : uint8_t {
    None     = 0,
    Size     = 1 << 0,
    Layout   = 1 << 1,
    Paint    = 1 << 2,
    Style    = 1 << 3,
    InlineSize = 1 << 4,
};

inline ContainFlag operator|(ContainFlag a, ContainFlag b) {
    return static_cast<ContainFlag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline ContainFlag operator&(ContainFlag a, ContainFlag b) {
    return static_cast<ContainFlag>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

struct ContainmentContext {
    ContainFlag flags = ContainFlag::None;
    bool contentVisibility = true; // content-visibility: visible/hidden/auto

    bool hasSizeContainment() const {
        return static_cast<uint8_t>(flags & ContainFlag::Size) != 0;
    }
    bool hasLayoutContainment() const {
        return static_cast<uint8_t>(flags & ContainFlag::Layout) != 0;
    }
    bool hasPaintContainment() const {
        return static_cast<uint8_t>(flags & ContainFlag::Paint) != 0;
    }
    bool hasStyleContainment() const {
        return static_cast<uint8_t>(flags & ContainFlag::Style) != 0;
    }
    bool hasInlineSizeContainment() const {
        return static_cast<uint8_t>(flags & ContainFlag::InlineSize) != 0;
    }

    static ContainFlag parse(const std::string& value);
};

// ==================================================================
// Content Visibility — content-visibility: auto
// ==================================================================

class ContentVisibilityManager {
public:
    struct Entry {
        void* element = nullptr;
        float estimatedHeight = 0;
        bool isVisible = false;
        bool isLocked = false; // content-visibility: hidden
        float intrinsicHeight = 0;
    };

    void registerElement(void* element, float estimatedHeight);
    void unregisterElement(void* element);

    void updateVisibility(float scrollTop, float viewportHeight);

    bool isVisible(void* element) const;
    float getHeight(void* element) const;

    // Total content height (for scrollbar)
    float totalContentHeight() const;

private:
    std::vector<Entry> entries_;
    float scrollTop_ = 0;
    float viewportHeight_ = 0;
};

// ==================================================================
// Web Font — @font-face descriptor
// ==================================================================

struct FontFaceDescriptor {
    std::string family;
    std::string src;        // url() or local()
    std::string style = "normal";   // normal, italic, oblique
    std::string weight = "normal";  // 100-900, normal, bold
    std::string stretch = "normal"; // condensed, expanded, etc.
    std::string unicodeRange = "U+0-10FFFF";
    std::string display = "auto";   // auto, block, swap, fallback, optional
    std::string sizeAdjust = "100%";
    std::string ascentOverride;
    std::string descentOverride;
    std::string lineGapOverride;

    // Parsed values
    int weightMin = 400, weightMax = 400;
    bool isItalic = false;
    float sizeAdjustFactor = 1.0f;
};

// ==================================================================
// Font load states
// ==================================================================

enum class FontLoadState : uint8_t {
    Unloaded, Loading, Loaded, Error
};

// ==================================================================
// Font Face — represents a single @font-face
// ==================================================================

class FontFace {
public:
    FontFace(const FontFaceDescriptor& desc);
    ~FontFace();

    const FontFaceDescriptor& descriptor() const { return desc_; }
    FontLoadState status() const { return status_; }
    const std::string& family() const { return desc_.family; }

    // Load the font data
    void load();
    void setData(const uint8_t* data, size_t size);
    void setError(const std::string& message);

    // Callbacks
    using LoadCallback = std::function<void(FontFace*)>;
    void onLoad(LoadCallback cb) { onLoad_ = cb; }
    void onError(LoadCallback cb) { onError_ = cb; }

    // Font metrics (available after load)
    struct Metrics {
        float unitsPerEm = 1000;
        float ascender = 0;
        float descender = 0;
        float lineGap = 0;
        float capHeight = 0;
        float xHeight = 0;
        float underlinePosition = 0;
        float underlineThickness = 0;
        float strikethroughPosition = 0;
        float strikethroughThickness = 0;
    };
    const Metrics& metrics() const { return metrics_; }

    // Weight matching
    bool matchesWeight(int weight) const;
    bool matchesStyle(const std::string& style) const;

    // Raw font data
    const std::vector<uint8_t>& data() const { return fontData_; }
    size_t dataSize() const { return fontData_.size(); }

private:
    FontFaceDescriptor desc_;
    FontLoadState status_ = FontLoadState::Unloaded;
    std::vector<uint8_t> fontData_;
    Metrics metrics_;
    std::string errorMessage_;
    LoadCallback onLoad_;
    LoadCallback onError_;

    void parseMetrics();
};

// ==================================================================
// Font Face Set — document.fonts
// ==================================================================

class FontFaceSet {
public:
    static FontFaceSet& instance();

    // Add/remove font faces
    void add(std::shared_ptr<FontFace> face);
    bool remove(std::shared_ptr<FontFace> face);
    void clear();

    // Query
    bool has(const std::string& family) const;
    bool check(const std::string& font, const std::string& text = "") const;

    // Load all matching
    void load(const std::string& font, const std::string& text = "");

    // Status
    enum class ReadyState { Loading, Loaded };
    ReadyState readyState() const;

    // Events
    using ReadyCallback = std::function<void()>;
    void onReady(ReadyCallback cb) { onReady_ = cb; }

    // Lookup
    std::vector<std::shared_ptr<FontFace>> match(const std::string& family,
                                                    int weight = 400,
                                                    const std::string& style = "normal") const;

    // Get all
    const std::vector<std::shared_ptr<FontFace>>& faces() const { return faces_; }

    // Font matching (CSS font matching algorithm)
    std::shared_ptr<FontFace> bestMatch(const std::string& family,
                                          int weight = 400,
                                          const std::string& style = "normal") const;

private:
    FontFaceSet() = default;
    std::vector<std::shared_ptr<FontFace>> faces_;
    size_t loadingCount_ = 0;
    ReadyCallback onReady_;
    mutable std::mutex mutex_;
};

// ==================================================================
// Font format detection
// ==================================================================

enum class FontFormat : uint8_t {
    Unknown, TrueType, OpenType, WOFF, WOFF2, EOT, SVG, Collection
};

class FontFormatDetector {
public:
    static FontFormat detect(const uint8_t* data, size_t size);
    static std::string mimeType(FontFormat format);
    static FontFormat fromExtension(const std::string& ext);
};

// ==================================================================
// WOFF2 decoder stub
// ==================================================================

class WOFF2Decoder {
public:
    static bool isWOFF2(const uint8_t* data, size_t size);
    static std::vector<uint8_t> decode(const uint8_t* data, size_t size);
};

// ==================================================================
// Selection/Range API — text selection rendering
// ==================================================================

struct SelectionPoint {
    void* node = nullptr;
    int offset = 0;
};

struct SelectionRange {
    SelectionPoint start;
    SelectionPoint end;
    bool collapsed = true;

    bool isCollapsed() const { return collapsed; }
};

class Selection {
public:
    Selection();
    ~Selection();

    // Range management
    void setRange(const SelectionRange& range);
    void collapse(void* node, int offset);
    void collapseToStart();
    void collapseToEnd();
    void extend(void* node, int offset);
    void selectAll();
    void removeAllRanges();

    int rangeCount() const { return ranges_.empty() ? 0 : 1; }
    const SelectionRange* getRangeAt(int index) const;
    bool isCollapsed() const;

    // Selected text
    std::string toString() const;

    // Direction
    enum class Direction { None, Forward, Backward } direction = Direction::None;

    // Focus/Anchor
    void* anchorNode() const;
    int anchorOffset() const;
    void* focusNode() const;
    int focusOffset() const;

    // Events
    using SelectionChangeCallback = std::function<void()>;
    void onSelectionChange(SelectionChangeCallback cb) { onChange_ = cb; }

    // Visual rects for rendering highlight
    struct HighlightRect {
        float x, y, width, height;
    };
    std::vector<HighlightRect> getHighlightRects() const { return highlightRects_; }
    void setHighlightRects(const std::vector<HighlightRect>& rects) { highlightRects_ = rects; }

private:
    std::vector<SelectionRange> ranges_;
    std::vector<HighlightRect> highlightRects_;
    SelectionChangeCallback onChange_;
};

// ==================================================================
// Clipboard API
// ==================================================================

struct ClipboardItem {
    std::string type; // MIME type
    std::vector<uint8_t> data;
    std::string textData;
};

class ClipboardAPI {
public:
    static ClipboardAPI& instance();

    // Async clipboard API
    void writeText(const std::string& text);
    std::string readText() const;

    void write(const std::vector<ClipboardItem>& items);
    std::vector<ClipboardItem> read() const;

    // Legacy
    bool execCommand(const std::string& command, const Selection& selection);

    // Events
    using ClipboardCallback = std::function<void(const std::string&)>;
    void onCopy(ClipboardCallback cb) { onCopy_ = cb; }
    void onPaste(ClipboardCallback cb) { onPaste_ = cb; }
    void onCut(ClipboardCallback cb) { onCut_ = cb; }

private:
    ClipboardAPI() = default;
    std::string clipboardText_;
    std::vector<ClipboardItem> clipboardItems_;
    ClipboardCallback onCopy_, onPaste_, onCut_;
};

} // namespace NXRender
