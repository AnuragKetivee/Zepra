// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "nxgfx/text/text_shaper.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <ft2build.h>
#include FT_FREETYPE_H

struct hb_font_t;

namespace NXRender {
namespace Text {

class FontFallbackManager {
public:
    static FontFallbackManager& instance();

    bool initialize();
    void shutdown();

    // Registers a font file with a family name and standard style parameters
    bool registerFont(const std::string& family, const std::string& path, bool isBold, bool isItalic);

    // Queries the most appropriate FT_Face for the given characters
    FT_Face getFace(const std::string& family, bool isBold, bool isItalic);
    
    // Returns a HarfBuzz wrapper for the given face
    hb_font_t* getHbFont(const std::string& family, bool isBold, bool isItalic, float size);

private:
    FontFallbackManager();
    ~FontFallbackManager();

    struct FontRegistration {
        std::string family;
        std::string path;
        bool isBold;
        bool isItalic;
    };

    FT_Library ftLibrary_ = nullptr;
    std::unordered_map<std::string, std::vector<FontRegistration>> registry_;
    std::unordered_map<std::string, FT_Face> faceCache_;
    std::unordered_map<std::string, hb_font_t*> hbFontCache_;

    std::string buildKey(const std::string& family, bool isBold, bool isItalic, float size) const;
};

} // namespace Text
} // namespace NXRender
