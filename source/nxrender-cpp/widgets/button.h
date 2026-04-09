// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file button.h
 * @brief Button widget
 */

#pragma once

#include "widget.h"
#include <functional>

namespace NXRender {

/**
 * @brief Button widget with hover, press states
 */
class Button : public Widget {
public:
    Button();
    explicit Button(const std::string& label);
    ~Button() override;
    
    // Label
    const std::string& label() const { return label_; }
    void setLabel(const std::string& label) { label_ = label; }
    
    // Style
    float cornerRadius() const { return cornerRadius_; }
    void setCornerRadius(float radius) { cornerRadius_ = radius; }
    
    Color textColor() const { return textColor_; }
    void setTextColor(const Color& color) { textColor_ = color; }

    float fontSize() const { return fontSize_; }
    void setFontSize(float size) { fontSize_ = size; }

    float borderWidth() const { return borderWidth_; }
    void setBorderWidth(float w) { borderWidth_ = w; }

    // Icon
    void setIcon(uint32_t textureId, float size = 16.0f) {
        iconTextureId_ = textureId;
        iconSize_ = size;
    }

    // Minimum size
    void setMinSize(float w, float h) { minWidth_ = w; minHeight_ = h; }
    
    // Callbacks
    using ClickHandler = std::function<void()>;
    void onClick(ClickHandler handler) { clickHandler_ = handler; }
    
    // Rendering
    void render(GpuContext* ctx) override;
    Size measure(const Size& available) override;
    
    // Events
    EventResult onMouseDown(float x, float y, MouseButton button) override;
    EventResult onMouseUp(float x, float y, MouseButton button) override;
    EventResult onMouseEnter() override;
    EventResult onMouseLeave() override;
    
private:
    Color resolveBackgroundColor() const;
    Color resolveTextColor() const;
    Color resolveBorderColor() const;

    std::string label_;
    float cornerRadius_ = 4.0f;
    float fontSize_ = 14.0f;
    float borderWidth_ = 0.0f;
    Color textColor_ = Color::white();
    ClickHandler clickHandler_;

    uint32_t iconTextureId_ = 0;
    float iconSize_ = 16.0f;
    float iconGap_ = 6.0f;
    float minWidth_ = 64.0f;
    float minHeight_ = 32.0f;
};

} // namespace NXRender
