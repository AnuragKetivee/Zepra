// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "event.h"
#include <string>
#include <map>
#include <functional>

namespace NXRender {
namespace Input {

using ShortcutAction = std::function<void()>;

struct ShortcutChord {
    std::string key;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool meta = false;

    // Parses strings like "Ctrl+Shift+T" or "Alt+F4"
    static ShortcutChord parse(const std::string& chordStr);

    bool operator<(const ShortcutChord& other) const {
        if (ctrl != other.ctrl) return ctrl < other.ctrl;
        if (shift != other.shift) return shift < other.shift;
        if (alt != other.alt) return alt < other.alt;
        if (meta != other.meta) return meta < other.meta;
        return key < other.key;
    }
};

class ShortcutManager {
public:
    ShortcutManager();
    ~ShortcutManager();

    // Register a global keyboard shortcut
    void registerShortcut(const std::string& chordStr, ShortcutAction action);
    
    // Remove a global shortcut
    void unregisterShortcut(const std::string& chordStr);

    // Processes a key event and triggers bound actions immediately.
    // Returns true if the event was consumed (preventing DOM routing).
    bool dispatch(const KeyEvent& event);

private:
    std::map<ShortcutChord, ShortcutAction> bindings_;

    bool match(const ShortcutChord& chord, const KeyEvent& event) const;
};

} // namespace Input
} // namespace NXRender
