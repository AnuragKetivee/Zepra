// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "widgets/button.h"
#include "nxgfx/context.h"
#include "theme/theme.h"
#include <cmath>
#include <algorithm>

namespace NXRender {

Button::Button() : label_("") {
    setPadding(EdgeInsets(8, 16, 8, 16));
}

Button::Button(const std::string& label) : label_(label) {
    setPadding(EdgeInsets(8, 16, 8, 16));
}

Button::~Button() = default;

// ==================================================================
// Color resolution — derives visual colors from base + state
// ==================================================================

static Color adjustBrightness(const Color& base, float factor) {
    auto clampByte = [](int v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(v, 0, 255));
    };
    return Color(
        clampByte(static_cast<int>(base.r * factor)),
        clampByte(static_cast<int>(base.g * factor)),
        clampByte(static_cast<int>(base.b * factor)),
        base.a
    );
}

static Color lerpColor(const Color& a, const Color& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color(
        static_cast<uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<uint8_t>(a.b + (b.b - a.b) * t),
        static_cast<uint8_t>(a.a + (b.a - a.a) * t)
    );
}

Color Button::resolveBackgroundColor() const {
    Color base = backgroundColor_;
    if (base.a == 0) {
        Theme* t = currentTheme();
        base = t ? t->colors.primary : Color(55, 120, 250);
    }

    if (!isEnabled()) {
        return Color(base.r, base.g, base.b, 128);
    }
    if (isPressed()) {
        return adjustBrightness(base, 0.7f);
    }
    if (isHovered()) {
        return adjustBrightness(base, 1.15f);
    }
    return base;
}

Color Button::resolveTextColor() const {
    if (!isEnabled()) {
        return Color(textColor_.r, textColor_.g, textColor_.b, 128);
    }
    return textColor_;
}

Color Button::resolveBorderColor() const {
    Color bg = resolveBackgroundColor();
    return adjustBrightness(bg, 0.6f);
}

// ==================================================================
// Render — full button paint with background, border, focus ring, text
// ==================================================================

void Button::render(GpuContext* ctx) {
    if (!isVisible()) return;

    Color bgColor = resolveBackgroundColor();
    Color txtColor = resolveTextColor();
    Color borderColor = resolveBorderColor();

    // Shadow (subtle drop shadow for elevation)
    if (isEnabled() && cornerRadius_ > 0) {
        Color shadowColor(0, 0, 0, 40);
        Rect shadowRect(bounds_.x + 1, bounds_.y + 2,
                        bounds_.width, bounds_.height);
        ctx->fillRoundedRect(shadowRect, shadowColor, cornerRadius_);
    }

    // Background
    if (cornerRadius_ > 0) {
        ctx->fillRoundedRect(bounds_, bgColor, cornerRadius_);
    } else {
        ctx->fillRect(bounds_, bgColor);
    }

    // Border
    if (borderWidth_ > 0) {
        // Top
        ctx->fillRect(Rect(bounds_.x, bounds_.y,
                          bounds_.width, borderWidth_), borderColor);
        // Bottom
        ctx->fillRect(Rect(bounds_.x, bounds_.y + bounds_.height - borderWidth_,
                          bounds_.width, borderWidth_), borderColor);
        // Left
        ctx->fillRect(Rect(bounds_.x, bounds_.y,
                          borderWidth_, bounds_.height), borderColor);
        // Right
        ctx->fillRect(Rect(bounds_.x + bounds_.width - borderWidth_, bounds_.y,
                          borderWidth_, bounds_.height), borderColor);
    }

    // Focus ring
    if (isFocused()) {
        Color focusColor(66, 133, 244, 180);
        float focusOffset = 2.0f;
        Rect focusRect(bounds_.x - focusOffset, bounds_.y - focusOffset,
                       bounds_.width + focusOffset * 2,
                       bounds_.height + focusOffset * 2);
        if (cornerRadius_ > 0) {
            // Draw outline (larger rounded rect behind)
            ctx->fillRoundedRect(focusRect, focusColor, cornerRadius_ + focusOffset);
            ctx->fillRoundedRect(bounds_, bgColor, cornerRadius_);
        }
    }

    // Icon (if set)
    if (iconTextureId_ != 0) {
        float iconX = bounds_.x + padding_.left;
        float iconY = bounds_.y + (bounds_.height - iconSize_) / 2.0f;
        Rect iconRect(iconX, iconY, iconSize_, iconSize_);
        ctx->drawTexture(iconTextureId_, iconRect);
    }

    // Text
    if (!label_.empty()) {
        float fontSize = fontSize_;
        Size textSize = ctx->measureText(label_, fontSize);

        // Center text, accounting for icon
        float textAreaX = bounds_.x + padding_.left;
        if (iconTextureId_ != 0) {
            textAreaX += iconSize_ + iconGap_;
        }
        float textAreaW = bounds_.x + bounds_.width - padding_.right - textAreaX;

        float textX = textAreaX + (textAreaW - textSize.width) / 2.0f;
        float textY = bounds_.y + (bounds_.height - textSize.height) / 2.0f
                      + textSize.height * 0.8f;

        ctx->drawText(label_, textX, textY, txtColor, fontSize);
    }

    // Pressed overlay (darken effect)
    if (isPressed()) {
        Color overlay(0, 0, 0, 30);
        if (cornerRadius_ > 0) {
            ctx->fillRoundedRect(bounds_, overlay, cornerRadius_);
        } else {
            ctx->fillRect(bounds_, overlay);
        }
    }
}

// ==================================================================
// Measurement
// ==================================================================

Size Button::measure(const Size& available) {
    (void)available;

    // Estimate text size
    float charWidth = fontSize_ * 0.6f;
    float textWidth = label_.size() * charWidth;
    float textHeight = fontSize_;

    float totalWidth = textWidth + padding_.horizontal();
    float totalHeight = textHeight + padding_.vertical();

    if (iconTextureId_ != 0) {
        totalWidth += iconSize_ + iconGap_;
        totalHeight = std::max(totalHeight, iconSize_ + padding_.vertical());
    }

    // Minimum button size
    totalWidth = std::max(totalWidth, minWidth_);
    totalHeight = std::max(totalHeight, minHeight_);

    return Size(totalWidth, totalHeight);
}

// ==================================================================
// Event handling
// ==================================================================

EventResult Button::onMouseDown(float x, float y, MouseButton button) {
    if (!isEnabled()) return EventResult::Ignored;
    if (button != MouseButton::Left) return EventResult::Ignored;
    if (!bounds_.contains(x, y)) return EventResult::Ignored;

    state_.pressed = true;
    return EventResult::NeedsRedraw;
}

EventResult Button::onMouseUp(float x, float y, MouseButton button) {
    if (!isEnabled()) return EventResult::Ignored;
    if (button != MouseButton::Left) return EventResult::Ignored;

    bool wasPressed = state_.pressed;
    state_.pressed = false;

    if (wasPressed && bounds_.contains(x, y)) {
        if (clickHandler_) {
            clickHandler_();
        }
        return EventResult::NeedsRedraw;
    }

    return EventResult::NeedsRedraw;
}

EventResult Button::onMouseEnter() {
    if (!isEnabled()) return EventResult::Ignored;
    state_.hovered = true;
    return EventResult::NeedsRedraw;
}

EventResult Button::onMouseLeave() {
    state_.hovered = false;
    state_.pressed = false;
    return EventResult::NeedsRedraw;
}

} // namespace NXRender
