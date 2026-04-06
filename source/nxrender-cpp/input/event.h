// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include <string>
#include <cstdint>

namespace NXRender {
namespace Input {

enum class EventType : uint32_t {
    Unknown = 0,
    // Mouse Events
    MouseDown,
    MouseUp,
    MouseMove,
    MouseEnter,
    MouseLeave,
    MouseWheel,
    Click,
    DoubleClick,
    // Keyboard Events
    KeyDown,
    KeyUp,
    KeyPress,
    // Focus Events
    FocusIn,
    FocusOut,
    // Drag & Drop
    DragStart,
    DragMove,
    DragEnd,
    Drop
};

enum class EventPhase : uint8_t {
    None = 0,
    CapturingPhase = 1,
    AtTarget = 2,
    BubblingPhase = 3
};

struct EventModifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool meta = false; // Windows / Command key
};

class EventTarget;

class Event {
public:
    explicit Event(EventType type, bool bubbles = true)
        : type_(type), bubbles_(bubbles) {}
    
    virtual ~Event() = default;

    EventType type() const { return type_; }
    bool bubbles() const { return bubbles_; }
    bool cancelable() const { return cancelable_; }
    
    // Phase Tracking
    EventPhase phase() const { return phase_; }
    void setPhase(EventPhase phase) { phase_ = phase; }

    // Routing targets
    EventTarget* target() const { return target_; }
    void setTarget(EventTarget* target) { target_ = target; }

    EventTarget* currentTarget() const { return currentTarget_; }
    void setCurrentTarget(EventTarget* target) { currentTarget_ = target; }

    // Propagation control
    void stopPropagation() { propagationStopped_ = true; }
    void stopImmediatePropagation() { propagationStopped_ = true; immediatePropagationStopped_ = true; }
    bool isPropagationStopped() const { return propagationStopped_; }
    bool isImmediatePropagationStopped() const { return immediatePropagationStopped_; }

    void preventDefault() { defaultPrevented_ = true; }
    bool isDefaultPrevented() const { return defaultPrevented_; }

protected:
    EventType type_;
    bool bubbles_;
    bool cancelable_ = true;
    EventPhase phase_ = EventPhase::None;

    EventTarget* target_ = nullptr;
    EventTarget* currentTarget_ = nullptr;

    bool propagationStopped_ = false;
    bool immediatePropagationStopped_ = false;
    bool defaultPrevented_ = false;
};

class MouseEvent : public Event {
public:
    MouseEvent(EventType type, float x, float y, int button, const EventModifiers& mods)
        : Event(type, true), x_(x), y_(y), button_(button), modifiers_(mods) {}

    float x() const { return x_; }
    float y() const { return y_; }
    int button() const { return button_; } // 0=Left, 1=Middle, 2=Right
    const EventModifiers& modifiers() const { return modifiers_; }

private:
    float x_, y_;
    int button_;
    EventModifiers modifiers_;
};

class ScrollEvent : public Event {
public:
    ScrollEvent(float deltaX, float deltaY, const EventModifiers& mods)
        : Event(EventType::MouseWheel, true), deltaX_(deltaX), deltaY_(deltaY), modifiers_(mods) {}

    float deltaX() const { return deltaX_; }
    float deltaY() const { return deltaY_; }
    const EventModifiers& modifiers() const { return modifiers_; }

private:
    float deltaX_, deltaY_;
    EventModifiers modifiers_;
};

class KeyEvent : public Event {
public:
    KeyEvent(EventType type, int keyCode, const std::string& keyStr, const EventModifiers& mods)
        : Event(type, true), keyCode_(keyCode), keyString_(keyStr), modifiers_(mods) {}

    int keyCode() const { return keyCode_; }
    const std::string& keyString() const { return keyString_; }
    const EventModifiers& modifiers() const { return modifiers_; }

private:
    int keyCode_;
    std::string keyString_;
    EventModifiers modifiers_;
};

} // namespace Input
} // namespace NXRender
