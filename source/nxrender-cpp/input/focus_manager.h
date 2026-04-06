// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "event.h"
#include "event_router.h"

namespace NXRender {
namespace Input {

enum class FocusOrigin {
    None,
    Mouse,
    Keyboard,
    Programmatic
};

class FocusManager {
public:
    FocusManager(EventRouter* router);
    ~FocusManager();

    void setFocusedTarget(EventTarget* target, FocusOrigin origin = FocusOrigin::Programmatic);
    EventTarget* focusedTarget() const { return currentFocus_; }
    
    // Core Focus Routing
    void clearFocus();
    bool requestFocusNext();
    bool requestFocusPrevious();

    // Determines if outline/ring should be drawn (usually false if clicked)
    bool isKeyboardFocused() const { return currentOrigin_ == FocusOrigin::Keyboard; }

    // Binds explicitly to the EventRouter's global dispatch pipeline to intercept Tabs
    bool handleGlobalKeyEvent(const KeyEvent& event);

private:
    EventRouter* router_;
    EventTarget* currentFocus_ = nullptr;
    FocusOrigin currentOrigin_ = FocusOrigin::None;

    // Helper to find next target dynamically without strict indexing.
    EventTarget* findNextFocusable(EventTarget* current, bool reverse) const;
};

} // namespace Input
} // namespace NXRender
