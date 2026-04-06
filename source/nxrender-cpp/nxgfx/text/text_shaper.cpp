// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "text_shaper.h"
#include "font_fallback.h"
#include <iostream>

namespace NXRender {
namespace Text {

TextShaper::TextShaper() {
    hbBuffer_ = hb_buffer_create();
}

TextShaper::~TextShaper() {
    if (hbBuffer_) {
        hb_buffer_destroy(hbBuffer_);
        hbBuffer_ = nullptr;
    }
}

bool TextShaper::init() {
    return FontFallbackManager::instance().initialize();
}

TextRun TextShaper::shape(const std::string& text, const std::string& fontName, bool isBold, bool isItalic, float fontSize, const std::string& lang, bool isRTL) {
    TextRun run;
    run.text = text;
    run.language = lang;
    run.isRTL = isRTL;

    hb_font_t* useFont = FontFallbackManager::instance().getHbFont(fontName, isBold, isItalic, fontSize);
    if (!useFont) {
        // Core fallback if exact font missing
        useFont = FontFallbackManager::instance().getHbFont("Arial", false, false, fontSize);
    }

    if (!useFont) return run;

    hb_buffer_clear_contents(hbBuffer_);
    
    // Add text to the buffer
    hb_buffer_add_utf8(hbBuffer_, text.c_str(), -1, 0, -1);
    
    // Setup script direction and language semantics
    hb_buffer_set_direction(hbBuffer_, isRTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_language(hbBuffer_, hb_language_from_string(lang.c_str(), -1));
    
    // Execute shaping
    hb_shape(useFont, hbBuffer_, nullptr, 0);
    
    unsigned int glyph_count;
    hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(hbBuffer_, &glyph_count);
    hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(hbBuffer_, &glyph_count);

    run.glyphs.reserve(glyph_count);

    // FT / HB scales native coordinates in 1/64th pixel units (26.6 fixed point format)
    const float HB_SCALE = 1.0f / 64.0f;

    for (unsigned int i = 0; i < glyph_count; ++i) {
        ShapedGlyph g;
        g.glyphIndex = glyph_info[i].codepoint;
        g.clusterIndex = glyph_info[i].cluster;
        
        g.xAdvance = glyph_pos[i].x_advance * HB_SCALE;
        g.yAdvance = glyph_pos[i].y_advance * HB_SCALE;
        g.xOffset = glyph_pos[i].x_offset * HB_SCALE;
        g.yOffset = glyph_pos[i].y_offset * HB_SCALE;
        
        run.glyphs.push_back(g);
    }
    
    return run;
}

} // namespace Text
} // namespace NXRender
