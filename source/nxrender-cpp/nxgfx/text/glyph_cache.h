// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include <unordered_map>
#include <string>
#include <cstdint>
#include "nxgfx/primitives.h"

namespace NXRender {
class GpuContext;

namespace Text {

struct GlyphTextureInfo {
    uint32_t textureId;
    Rect uvBounds;
    float width;
    float height;
    float bearingX;
    float bearingY;
    float advance;
};

// Responsible for rasterizing FT_Face glyphs and packing them into an atlas texture array.
class GlyphCache {
public:
    static GlyphCache& instance();

    bool initialize(GpuContext* ctx);
    void shutdown();

    // Requests a single rasterized glyph descriptor. If missing, it will render it onto the GPU atlas inline.
    const GlyphTextureInfo* getGlyph(const std::string& family, bool isBold, bool isItalic, float size, uint32_t glyphIndex);

private:
    GlyphCache();
    ~GlyphCache();

    GpuContext* ctx_ = nullptr;
    std::unordered_map<uint64_t, GlyphTextureInfo> glyphMap_;
    
    // Pack structures for atlas
    uint32_t atlasTextureId_ = 0;
    int currentAtlasX_ = 0;
    int currentAtlasY_ = 0;
    int currentAtlasRowHeight_ = 0;
    const int ATLAS_SIZE = 1024;

    uint64_t buildKey(uint32_t fontId, float size, uint32_t glyphIndex) const;
    void growAtlas();
};

} // namespace Text
} // namespace NXRender
