// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "containment.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace NXRender {

// ==================================================================
// ContainmentContext
// ==================================================================

ContainFlag ContainmentContext::parse(const std::string& value) {
    if (value == "none") return ContainFlag::None;
    if (value == "strict") return ContainFlag::Size | ContainFlag::Layout | ContainFlag::Paint | ContainFlag::Style;
    if (value == "content") return ContainFlag::Layout | ContainFlag::Paint | ContainFlag::Style;

    ContainFlag flags = ContainFlag::None;
    std::istringstream ss(value);
    std::string token;
    while (ss >> token) {
        if (token == "size") flags = flags | ContainFlag::Size;
        else if (token == "layout") flags = flags | ContainFlag::Layout;
        else if (token == "paint") flags = flags | ContainFlag::Paint;
        else if (token == "style") flags = flags | ContainFlag::Style;
        else if (token == "inline-size") flags = flags | ContainFlag::InlineSize;
    }
    return flags;
}

// ==================================================================
// ContentVisibilityManager
// ==================================================================

void ContentVisibilityManager::registerElement(void* element, float estimatedHeight) {
    for (auto& e : entries_) {
        if (e.element == element) {
            e.estimatedHeight = estimatedHeight;
            return;
        }
    }
    entries_.push_back({element, estimatedHeight, false, false, 0});
}

void ContentVisibilityManager::unregisterElement(void* element) {
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                        [element](const Entry& e) { return e.element == element; }),
        entries_.end());
}

void ContentVisibilityManager::updateVisibility(float scrollTop, float viewportHeight) {
    scrollTop_ = scrollTop;
    viewportHeight_ = viewportHeight;

    float y = 0;
    float viewBottom = scrollTop + viewportHeight;
    float margin = viewportHeight; // Render one viewport ahead/behind

    for (auto& entry : entries_) {
        if (entry.isLocked) {
            entry.isVisible = false;
            y += entry.estimatedHeight;
            continue;
        }

        float entryBottom = y + (entry.intrinsicHeight > 0 ? entry.intrinsicHeight : entry.estimatedHeight);
        bool visible = (entryBottom > scrollTop - margin && y < viewBottom + margin);
        entry.isVisible = visible;

        y = entryBottom;
    }
}

bool ContentVisibilityManager::isVisible(void* element) const {
    for (const auto& e : entries_) {
        if (e.element == element) return e.isVisible;
    }
    return true;
}

float ContentVisibilityManager::getHeight(void* element) const {
    for (const auto& e : entries_) {
        if (e.element == element) {
            if (e.isVisible && e.intrinsicHeight > 0) return e.intrinsicHeight;
            return e.estimatedHeight;
        }
    }
    return 0;
}

float ContentVisibilityManager::totalContentHeight() const {
    float total = 0;
    for (const auto& e : entries_) {
        total += (e.intrinsicHeight > 0 && e.isVisible) ? e.intrinsicHeight : e.estimatedHeight;
    }
    return total;
}

// ==================================================================
// FontFace
// ==================================================================

FontFace::FontFace(const FontFaceDescriptor& desc) : desc_(desc) {
    // Parse weight range
    if (desc.weight == "normal") { desc_.weightMin = desc_.weightMax = 400; }
    else if (desc.weight == "bold") { desc_.weightMin = desc_.weightMax = 700; }
    else {
        int w = std::atoi(desc.weight.c_str());
        if (w > 0) { desc_.weightMin = desc_.weightMax = w; }
    }
    desc_.isItalic = (desc.style == "italic" || desc.style == "oblique");

    if (!desc.sizeAdjust.empty()) {
        desc_.sizeAdjustFactor = std::strtof(desc.sizeAdjust.c_str(), nullptr) / 100.0f;
    }
}

FontFace::~FontFace() {}

void FontFace::load() {
    status_ = FontLoadState::Loading;
    // Actual loading delegated to resource loader
}

void FontFace::setData(const uint8_t* data, size_t size) {
    fontData_.assign(data, data + size);

    // Check for WOFF2 and decompress
    if (WOFF2Decoder::isWOFF2(data, size)) {
        auto decoded = WOFF2Decoder::decode(data, size);
        if (!decoded.empty()) {
            fontData_ = std::move(decoded);
        }
    }

    parseMetrics();
    status_ = FontLoadState::Loaded;
    if (onLoad_) onLoad_(this);
}

void FontFace::setError(const std::string& message) {
    errorMessage_ = message;
    status_ = FontLoadState::Error;
    if (onError_) onError_(this);
}

bool FontFace::matchesWeight(int weight) const {
    return weight >= desc_.weightMin && weight <= desc_.weightMax;
}

bool FontFace::matchesStyle(const std::string& style) const {
    if (style == "italic" || style == "oblique") return desc_.isItalic;
    return !desc_.isItalic;
}

void FontFace::parseMetrics() {
    if (fontData_.size() < 12) return;

    // Detect format
    const uint8_t* data = fontData_.data();
    size_t size = fontData_.size();

    // TrueType / OpenType: read 'head' and 'hhea' tables
    uint32_t sfVersion = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    bool isTrueType = (sfVersion == 0x00010000 || sfVersion == 0x74727565); // 1.0 or 'true'
    bool isOTF = (sfVersion == 0x4F54544F); // 'OTTO'

    if (!isTrueType && !isOTF) return;

    uint16_t numTables = (data[4] << 8) | data[5];

    // Scan table directory for 'head' and 'hhea' and 'OS/2'
    auto findTable = [&](const char* tag) -> std::pair<uint32_t, uint32_t> {
        for (uint16_t i = 0; i < numTables && (12 + i * 16 + 16) <= size; i++) {
            size_t offset = 12 + i * 16;
            if (std::memcmp(data + offset, tag, 4) == 0) {
                uint32_t tableOffset = (data[offset + 8] << 24) | (data[offset + 9] << 16) |
                                        (data[offset + 10] << 8) | data[offset + 11];
                uint32_t tableLength = (data[offset + 12] << 24) | (data[offset + 13] << 16) |
                                        (data[offset + 14] << 8) | data[offset + 15];
                return {tableOffset, tableLength};
            }
        }
        return {0, 0};
    };

    auto readInt16 = [&](size_t pos) -> int16_t {
        if (pos + 2 > size) return 0;
        return static_cast<int16_t>((data[pos] << 8) | data[pos + 1]);
    };

    auto readUint16 = [&](size_t pos) -> uint16_t {
        if (pos + 2 > size) return 0;
        return (data[pos] << 8) | data[pos + 1];
    };

    // head table: unitsPerEm at offset 18
    auto [headOff, headLen] = findTable("head");
    if (headOff && headLen >= 54) {
        metrics_.unitsPerEm = readUint16(headOff + 18);
    }

    // hhea table: ascender/descender/lineGap
    auto [hheaOff, hheaLen] = findTable("hhea");
    if (hheaOff && hheaLen >= 36) {
        metrics_.ascender = readInt16(hheaOff + 4);
        metrics_.descender = readInt16(hheaOff + 6);
        metrics_.lineGap = readInt16(hheaOff + 8);
    }

    // OS/2 table: more refined metrics
    auto [os2Off, os2Len] = findTable("OS/2");
    if (os2Off && os2Len >= 72) {
        int16_t sTypoAscender = readInt16(os2Off + 68);
        int16_t sTypoDescender = readInt16(os2Off + 70);
        if (sTypoAscender != 0) {
            metrics_.ascender = sTypoAscender;
            metrics_.descender = sTypoDescender;
        }
        if (os2Len >= 88) {
            metrics_.capHeight = readInt16(os2Off + 88);
        }
        if (os2Len >= 90) {
            metrics_.xHeight = readInt16(os2Off + 86);
        }
    }

    // post table: underline metrics
    auto [postOff, postLen] = findTable("post");
    if (postOff && postLen >= 12) {
        metrics_.underlinePosition = readInt16(postOff + 8);
        metrics_.underlineThickness = readInt16(postOff + 10);
    }
}

// ==================================================================
// FontFaceSet
// ==================================================================

FontFaceSet& FontFaceSet::instance() {
    static FontFaceSet inst;
    return inst;
}

void FontFaceSet::add(std::shared_ptr<FontFace> face) {
    std::lock_guard<std::mutex> lock(mutex_);
    faces_.push_back(std::move(face));
}

bool FontFaceSet::remove(std::shared_ptr<FontFace> face) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find(faces_.begin(), faces_.end(), face);
    if (it != faces_.end()) { faces_.erase(it); return true; }
    return false;
}

void FontFaceSet::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    faces_.clear();
}

bool FontFaceSet::has(const std::string& family) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& f : faces_) {
        if (f->family() == family) return true;
    }
    return false;
}

bool FontFaceSet::check(const std::string& font, const std::string& /*text*/) const {
    // Parse font shorthand "weight size family"
    std::istringstream ss(font);
    std::string token;
    std::string family;
    while (ss >> token) family = token; // Last token is family

    return has(family);
}

void FontFaceSet::load(const std::string& font, const std::string& /*text*/) {
    std::istringstream ss(font);
    std::string token;
    std::string family;
    while (ss >> token) family = token;

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& f : faces_) {
        if (f->family() == family && f->status() == FontLoadState::Unloaded) {
            loadingCount_++;
            f->onLoad([this](FontFace*) {
                loadingCount_--;
                if (loadingCount_ == 0 && onReady_) onReady_();
            });
            f->load();
        }
    }
}

FontFaceSet::ReadyState FontFaceSet::readyState() const {
    return (loadingCount_ > 0) ? ReadyState::Loading : ReadyState::Loaded;
}

std::vector<std::shared_ptr<FontFace>> FontFaceSet::match(const std::string& family,
                                                             int weight,
                                                             const std::string& style) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<FontFace>> result;
    for (const auto& f : faces_) {
        if (f->family() == family && f->matchesWeight(weight) && f->matchesStyle(style)) {
            result.push_back(f);
        }
    }
    return result;
}

std::shared_ptr<FontFace> FontFaceSet::bestMatch(const std::string& family,
                                                    int weight,
                                                    const std::string& style) const {
    auto matches = match(family, weight, style);
    if (matches.empty()) {
        // Try without style constraint
        matches = match(family, weight);
    }
    if (matches.empty()) {
        // Try any weight in family
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& f : faces_) {
            if (f->family() == family) return f;
        }
        return nullptr;
    }

    // Find closest weight
    std::shared_ptr<FontFace> best;
    int bestDist = 10000;
    for (const auto& f : matches) {
        int mid = (f->descriptor().weightMin + f->descriptor().weightMax) / 2;
        int dist = std::abs(mid - weight);
        if (dist < bestDist) {
            bestDist = dist;
            best = f;
        }
    }
    return best;
}

// ==================================================================
// FontFormatDetector
// ==================================================================

FontFormat FontFormatDetector::detect(const uint8_t* data, size_t size) {
    if (size < 4) return FontFormat::Unknown;

    // WOFF2
    if (data[0] == 'w' && data[1] == 'O' && data[2] == 'F' && data[3] == '2')
        return FontFormat::WOFF2;
    // WOFF
    if (data[0] == 'w' && data[1] == 'O' && data[2] == 'F' && data[3] == 'F')
        return FontFormat::WOFF;
    // TrueType
    uint32_t sfVersion = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    if (sfVersion == 0x00010000 || sfVersion == 0x74727565)
        return FontFormat::TrueType;
    // OpenType
    if (data[0] == 'O' && data[1] == 'T' && data[2] == 'T' && data[3] == 'O')
        return FontFormat::OpenType;
    // TrueType Collection
    if (data[0] == 't' && data[1] == 't' && data[2] == 'c' && data[3] == 'f')
        return FontFormat::Collection;
    // EOT
    if (size > 34 && data[34] == 'L' && data[35] == 'P')
        return FontFormat::EOT;

    return FontFormat::Unknown;
}

std::string FontFormatDetector::mimeType(FontFormat format) {
    switch (format) {
        case FontFormat::TrueType: return "font/ttf";
        case FontFormat::OpenType: return "font/otf";
        case FontFormat::WOFF: return "font/woff";
        case FontFormat::WOFF2: return "font/woff2";
        case FontFormat::Collection: return "font/collection";
        default: return "application/octet-stream";
    }
}

FontFormat FontFormatDetector::fromExtension(const std::string& ext) {
    if (ext == ".ttf") return FontFormat::TrueType;
    if (ext == ".otf") return FontFormat::OpenType;
    if (ext == ".woff") return FontFormat::WOFF;
    if (ext == ".woff2") return FontFormat::WOFF2;
    if (ext == ".eot") return FontFormat::EOT;
    if (ext == ".ttc") return FontFormat::Collection;
    return FontFormat::Unknown;
}

// ==================================================================
// WOFF2Decoder
// ==================================================================

bool WOFF2Decoder::isWOFF2(const uint8_t* data, size_t size) {
    return size >= 4 && data[0] == 'w' && data[1] == 'O' && data[2] == 'F' && data[3] == '2';
}

std::vector<uint8_t> WOFF2Decoder::decode(const uint8_t* data, size_t size) {
    // WOFF2 uses Brotli compression
    // Real implementation would use the brotli library
    // Structure: WOFF2Header → TableDirectory → CompressedData

    if (!isWOFF2(data, size) || size < 48) return {};

    // Parse WOFF2 header
    // uint32_t signature = read at 0
    // uint32_t flavor = read at 4
    // uint32_t length = read at 8
    // uint16_t numTables = read at 12
    // uint32_t totalSfntSize = read at 16 (uncompressed size)

    uint32_t totalSfntSize = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];

    // Placeholder: return empty buffer of correct size
    // Real decode requires Brotli decompression
    std::vector<uint8_t> result(totalSfntSize, 0);
    (void)data; (void)size;
    return result;
}

// ==================================================================
// Selection
// ==================================================================

Selection::Selection() {}
Selection::~Selection() {}

void Selection::setRange(const SelectionRange& range) {
    ranges_.clear();
    ranges_.push_back(range);
    if (onChange_) onChange_();
}

void Selection::collapse(void* node, int offset) {
    SelectionRange range;
    range.start = {node, offset};
    range.end = {node, offset};
    range.collapsed = true;
    setRange(range);
}

void Selection::collapseToStart() {
    if (!ranges_.empty()) {
        auto& r = ranges_[0];
        r.end = r.start;
        r.collapsed = true;
        if (onChange_) onChange_();
    }
}

void Selection::collapseToEnd() {
    if (!ranges_.empty()) {
        auto& r = ranges_[0];
        r.start = r.end;
        r.collapsed = true;
        if (onChange_) onChange_();
    }
}

void Selection::extend(void* node, int offset) {
    if (ranges_.empty()) {
        collapse(node, offset);
        return;
    }
    auto& r = ranges_[0];
    r.end = {node, offset};
    r.collapsed = (r.start.node == r.end.node && r.start.offset == r.end.offset);
    if (onChange_) onChange_();
}

void Selection::selectAll() {
    // Requires document context — set broad range
    SelectionRange range;
    range.collapsed = false;
    setRange(range);
}

void Selection::removeAllRanges() {
    ranges_.clear();
    highlightRects_.clear();
    if (onChange_) onChange_();
}

const SelectionRange* Selection::getRangeAt(int index) const {
    if (index >= 0 && static_cast<size_t>(index) < ranges_.size()) {
        return &ranges_[index];
    }
    return nullptr;
}

bool Selection::isCollapsed() const {
    return ranges_.empty() || ranges_[0].collapsed;
}

std::string Selection::toString() const {
    return ""; // Requires DOM text extraction
}

void* Selection::anchorNode() const {
    return ranges_.empty() ? nullptr : ranges_[0].start.node;
}

int Selection::anchorOffset() const {
    return ranges_.empty() ? 0 : ranges_[0].start.offset;
}

void* Selection::focusNode() const {
    return ranges_.empty() ? nullptr : ranges_[0].end.node;
}

int Selection::focusOffset() const {
    return ranges_.empty() ? 0 : ranges_[0].end.offset;
}

// ==================================================================
// ClipboardAPI
// ==================================================================

ClipboardAPI& ClipboardAPI::instance() {
    static ClipboardAPI inst;
    return inst;
}

void ClipboardAPI::writeText(const std::string& text) {
    clipboardText_ = text;
    clipboardItems_.clear();
    ClipboardItem item;
    item.type = "text/plain";
    item.textData = text;
    clipboardItems_.push_back(item);
}

std::string ClipboardAPI::readText() const {
    return clipboardText_;
}

void ClipboardAPI::write(const std::vector<ClipboardItem>& items) {
    clipboardItems_ = items;
    for (const auto& item : items) {
        if (item.type == "text/plain") {
            clipboardText_ = item.textData;
            break;
        }
    }
}

std::vector<ClipboardItem> ClipboardAPI::read() const {
    return clipboardItems_;
}

bool ClipboardAPI::execCommand(const std::string& command, const Selection& selection) {
    if (command == "copy") {
        std::string text = selection.toString();
        writeText(text);
        if (onCopy_) onCopy_(text);
        return true;
    }
    if (command == "cut") {
        std::string text = selection.toString();
        writeText(text);
        if (onCut_) onCut_(text);
        return true;
    }
    if (command == "paste") {
        if (onPaste_) onPaste_(clipboardText_);
        return true;
    }
    return false;
}

} // namespace NXRender
