// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "font_fallback.h"
#include <iostream>
#include <hb.h>
#include <hb-ft.h>

namespace NXRender {
namespace Text {

FontFallbackManager& FontFallbackManager::instance() {
    static FontFallbackManager instance;
    return instance;
}

FontFallbackManager::FontFallbackManager() {}

FontFallbackManager::~FontFallbackManager() {
    shutdown();
}

bool FontFallbackManager::initialize() {
    if (ftLibrary_) return true;
    if (FT_Init_FreeType(&ftLibrary_) != 0) {
        std::cerr << "Failed to initialize FreeType Library" << std::endl;
        return false;
    }
    return true;
}

void FontFallbackManager::shutdown() {
    for (auto& pair : hbFontCache_) {
        hb_font_destroy(pair.second);
    }
    hbFontCache_.clear();

    for (auto& pair : faceCache_) {
        FT_Done_Face(pair.second);
    }
    faceCache_.clear();

    if (ftLibrary_) {
        FT_Done_FreeType(ftLibrary_);
        ftLibrary_ = nullptr;
    }
}

bool FontFallbackManager::registerFont(const std::string& family, const std::string& path, bool isBold, bool isItalic) {
    registry_[family].push_back({family, path, isBold, isItalic});
    return true;
}

std::string FontFallbackManager::buildKey(const std::string& family, bool isBold, bool isItalic, float size) const {
    return family + (isBold ? "_B" : "_N") + (isItalic ? "_I" : "_N") + "_" + std::to_string(size);
}

FT_Face FontFallbackManager::getFace(const std::string& family, bool isBold, bool isItalic) {
    std::string key = buildKey(family, isBold, isItalic, 0.0f);
    
    if (faceCache_.find(key) != faceCache_.end()) {
        return faceCache_[key];
    }

    auto it = registry_.find(family);
    if (it == registry_.end()) {
        // Fallback to generically named system font if unavailable
        return nullptr;
    }

    const std::vector<FontRegistration>& fonts = it->second;
    const FontRegistration* bestMatch = nullptr;

    // Weight matching
    for (const auto& reg : fonts) {
        if (reg.isBold == isBold && reg.isItalic == isItalic) {
            bestMatch = &reg;
            break;
        }
    }
    if (!bestMatch && !fonts.empty()) bestMatch = &fonts[0];

    if (bestMatch) {
        FT_Face face;
        if (FT_New_Face(ftLibrary_, bestMatch->path.c_str(), 0, &face) == 0) {
            faceCache_[key] = face;
            return face;
        }
    }

    return nullptr;
}

hb_font_t* FontFallbackManager::getHbFont(const std::string& family, bool isBold, bool isItalic, float size) {
    std::string key = buildKey(family, isBold, isItalic, size);
    
    if (hbFontCache_.find(key) != hbFontCache_.end()) {
        return hbFontCache_[key];
    }

    FT_Face face = getFace(family, isBold, isItalic);
    if (!face) return nullptr;

    FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(size * 64.0f), 96, 96);

    hb_font_t* hbFont = hb_ft_font_create(face, nullptr);
    if (hbFont) {
        hb_ft_font_set_load_flags(hbFont, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
        hbFontCache_[key] = hbFont;
        return hbFont;
    }

    return nullptr;
}

} // namespace Text
} // namespace NXRender
