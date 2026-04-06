// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "event_router.h"
#include <algorithm>

namespace NXRender {
namespace Input {

EventRouter::EventRouter(EventTarget* rootTarget) : rootTarget_(rootTarget) {}

EventRouter::~EventRouter() {}

void EventRouter::setRootTarget(EventTarget* target) {
    rootTarget_ = target;
}

void EventRouter::setModalBarrier(EventTarget* modalTarget) {
    modalBarrier_ = modalTarget;
}

void EventRouter::clearModalBarrier() {
    modalBarrier_ = nullptr;
}

void EventRouter::setPointerState(EventTarget* currentHover) {
    if (hoverTarget_ == currentHover) return;

    EventModifiers emptyMods;
    
    // Leave sequence
    if (hoverTarget_) {
        MouseEvent leaveEvent(EventType::MouseLeave, 0, 0, -1, emptyMods);
        leaveEvent.setTarget(hoverTarget_);
        // MouseLeave historically does not bubble, so direct dispatch
        leaveEvent.setPhase(EventPhase::AtTarget);
        leaveEvent.setCurrentTarget(hoverTarget_);
        executeListeners(hoverTarget_, leaveEvent, EventPhase::AtTarget);
    }
    
    hoverTarget_ = currentHover;

    // Enter sequence
    if (hoverTarget_) {
        MouseEvent enterEvent(EventType::MouseEnter, 0, 0, -1, emptyMods);
        enterEvent.setTarget(hoverTarget_);
        enterEvent.setPhase(EventPhase::AtTarget);
        enterEvent.setCurrentTarget(hoverTarget_);
        executeListeners(hoverTarget_, enterEvent, EventPhase::AtTarget);
    }
}

std::vector<EventTarget*> EventRouter::buildPropagationPath(EventTarget* target) const {
    std::vector<EventTarget*> path;
    EventTarget* current = target;
    
    while (current) {
        path.push_back(current);
        if (current == rootTarget_ || current == modalBarrier_) {
            break; 
        }
        current = current->getParentTarget();
    }
    
    std::reverse(path.begin(), path.end());
    return path;
}

void EventRouter::executeListeners(EventTarget* target, Event& event, EventPhase matchingPhase) {
    bool isCapture = (matchingPhase == EventPhase::CapturingPhase);
    
    // We iterate over a copy of the listeners to prevent mutation logic bugs
    const auto listeners = target->listeners();
    
    for (const auto& listener : listeners) {
        if (event.isImmediatePropagationStopped()) {
            break;
        }

        if (listener.type == event.type() && listener.useCapture == isCapture) {
            listener.callback(event);
        }
    }
}

bool EventRouter::dispatchEvent(EventTarget* target, Event& event) {
    if (!target) return false;

    event.setTarget(target);
    std::vector<EventTarget*> path = buildPropagationPath(target);

    // 1. CAPTURING PHASE
    // Proceeds top-down from Root to Target parent
    event.setPhase(EventPhase::CapturingPhase);
    for (size_t i = 0; i < path.size(); ++i) {
        if (event.isPropagationStopped()) break;
        if (path[i] == target) break; // Don't run capture on target itself yet

        event.setCurrentTarget(path[i]);
        executeListeners(path[i], event, EventPhase::CapturingPhase);
    }

    // 2. AT TARGET
    // Executes locally, respecting both Capture and Bubble listeners bound directly on the target
    if (!event.isPropagationStopped()) {
        event.setPhase(EventPhase::AtTarget);
        event.setCurrentTarget(target);
        executeListeners(target, event, EventPhase::CapturingPhase); // Run capture binds on target
        
        if (!event.isImmediatePropagationStopped()) {
            executeListeners(target, event, EventPhase::BubblingPhase); // Run bubble binds on target
        }
    }

    // 3. BUBBLING PHASE
    // Proceeds bottom-up from Target parent to Root
    if (event.bubbles() && !event.isPropagationStopped()) {
        event.setPhase(EventPhase::BubblingPhase);
        for (int i = static_cast<int>(path.size()) - 2; i >= 0; --i) {
            if (event.isPropagationStopped()) break;

            event.setCurrentTarget(path[i]);
            executeListeners(path[i], event, EventPhase::BubblingPhase);
        }
    }

    return !event.isDefaultPrevented();
}

bool EventRouter::dispatchSpatialEvent(float localX, float localY, Event& event) {
    EventTarget* searchRoot = modalBarrier_ ? modalBarrier_ : rootTarget_;
    if (!searchRoot) return false;

    // Strict Hit-Testing
    if (searchRoot->hitTest(localX, localY)) {
        EventTarget* hitTarget = searchRoot->hitTestDeep(localX, localY);
        if (!hitTarget) hitTarget = searchRoot;
        
        // Update enter/leave routing context
        if (event.type() == EventType::MouseMove) {
            setPointerState(hitTarget);
        }

        return dispatchEvent(hitTarget, event);
    } else {
        // If the mouse missed the modal barrier completely, typically a hit-test miss.
        if (event.type() == EventType::MouseMove) setPointerState(nullptr);
    }
    
    return false;
}

} // namespace Input
} // namespace NXRender
