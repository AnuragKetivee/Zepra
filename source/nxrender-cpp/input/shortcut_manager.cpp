// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "shortcut_manager.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace NXRender {
namespace Input {

ShortcutManager::ShortcutManager() {}

ShortcutManager::~ShortcutManager() {}

ShortcutChord ShortcutChord::parse(const std::string& chordStr) {
    ShortcutChord chord;
    std::stringstream ss(chordStr);
    std::string token;

    while (std::getline(ss, token, '+')) {
        // Trim whitespace and optionally handle casing
        token.erase(0, token.find_first_not_of(" \t\n\r"));
        token.erase(token.find_last_not_of(" \t\n\r") + 1);
        
        std::string lowered = token;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char c){ return std::tolower(c); });

        if (lowered == "ctrl") chord.ctrl = true;
        else if (lowered == "shift") chord.shift = true;
        else if (lowered == "alt") chord.alt = true;
        else if (lowered == "meta" || lowered == "cmd" || lowered == "win") chord.meta = true;
        else {
            chord.key = token; // Keep original casing for exact match
        }
    }
    return chord;
}

void ShortcutManager::registerShortcut(const std::string& chordStr, ShortcutAction action) {
    bindings_[ShortcutChord::parse(chordStr)] = std::move(action);
}

void ShortcutManager::unregisterShortcut(const std::string& chordStr) {
    bindings_.erase(ShortcutChord::parse(chordStr));
}

bool ShortcutManager::match(const ShortcutChord& chord, const KeyEvent& event) const {
    if (chord.ctrl != event.modifiers().ctrl) return false;
    if (chord.shift != event.modifiers().shift) return false;
    if (chord.alt != event.modifiers().alt) return false;
    if (chord.meta != event.modifiers().meta) return false;

    // Case-insensitive key string match for shortcuts (e.g. 'T' matches 't')
    std::string chordKey = chord.key;
    std::string eventKey = event.keyString();
    
    std::transform(chordKey.begin(), chordKey.end(), chordKey.begin(), [](unsigned char c){ return std::tolower(c); });
    std::transform(eventKey.begin(), eventKey.end(), eventKey.begin(), [](unsigned char c){ return std::tolower(c); });

    return chordKey == eventKey;
}

bool ShortcutManager::dispatch(const KeyEvent& event) {
    if (event.type() != EventType::KeyDown) return false;

    // Evaluate all bindings to find a match
    for (const auto& [chord, action] : bindings_) {
        if (match(chord, event)) {
            if (action) {
                action();
            }
            return true; // Stop propagation/consume
        }
    }

    return false;
}

} // namespace Input
} // namespace NXRender
