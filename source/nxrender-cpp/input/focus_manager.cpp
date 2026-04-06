// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "focus_manager.h"

namespace NXRender {
namespace Input {

FocusManager::FocusManager(EventRouter* router) : router_(router) {}

FocusManager::~FocusManager() {
    clearFocus();
}

void FocusManager::clearFocus() {
    if (!currentFocus_) return;

    EventTarget* oldFocus = currentFocus_;
    currentFocus_ = nullptr;
    currentOrigin_ = FocusOrigin::None;

    EventModifiers emptyMods;
    Event blurEvent(EventType::FocusOut, false); // Focus events typically do not bubble
    blurEvent.setTarget(oldFocus);
    blurEvent.setPhase(EventPhase::AtTarget);
    blurEvent.setCurrentTarget(oldFocus);
    
    // We manually dispatch it avoiding the tree traversal, strict to the target
    if (router_) {
        // Technically FocusEvent does NOT bubble. The EventRouter dispatchEvent respects this.
        router_->dispatchEvent(oldFocus, blurEvent);
    }
}

void FocusManager::setFocusedTarget(EventTarget* target, FocusOrigin origin) {
    if (currentFocus_ == target) {
        // Update origin so rings appear dynamically if a user switches from mouse to keyboard
        currentOrigin_ = origin;
        return;
    }

    clearFocus();

    if (target) {
        currentFocus_ = target;
        currentOrigin_ = origin;

        EventModifiers emptyMods;
        Event focusEvent(EventType::FocusIn, false);
        
        if (router_) {
            router_->dispatchEvent(target, focusEvent);
        }
    }
}

EventTarget* FocusManager::findNextFocusable(EventTarget* current, bool reverse) const {
    // In a full implementation, this traverses the actual Node tree depth-first.
    // Since EventTarget is an abstract interface without child traversals defined,
    // this relies on the broader Window/Widget tree to supply the focus chain.
    // For now, it returns nullptr unless bound to a concrete DOM/Widget manager.
    return nullptr; 
}

bool FocusManager::requestFocusNext() {
    EventTarget* next = findNextFocusable(currentFocus_, false);
    if (next) {
        setFocusedTarget(next, FocusOrigin::Keyboard);
        return true;
    }
    return false;
}

bool FocusManager::requestFocusPrevious() {
    EventTarget* prev = findNextFocusable(currentFocus_, true);
    if (prev) {
        setFocusedTarget(prev, FocusOrigin::Keyboard);
        return true;
    }
    return false;
}

bool FocusManager::handleGlobalKeyEvent(const KeyEvent& event) {
    // Intercept hardware Tab for strict Focus Management
    if (event.type() == EventType::KeyDown && event.keyString() == "Tab") {
        if (event.modifiers().shift) {
            requestFocusPrevious();
        } else {
            requestFocusNext();
        }
        return true; // Stop propagation, consumed by FocusManager
    }
    return false;
}

} // namespace Input
} // namespace NXRender
