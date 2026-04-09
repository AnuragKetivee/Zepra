// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "widgets/label.h"
#include "nxgfx/context.h"
#include "theme/theme.h"
#include <algorithm>
#include <sstream>

namespace NXRender {

Label::Label() : text_("") {}

Label::Label(const std::string& text) : text_(text) {}

Label::~Label() = default;

// ==================================================================
// Text line breaking for word wrap
// ==================================================================

static std::vector<std::string> breakLines(const std::string& text, float maxWidth,
                                            float fontSize, GpuContext* ctx) {
    std::vector<std::string> lines;
    if (text.empty() || maxWidth <= 0) {
        lines.push_back(text);
        return lines;
    }

    size_t lineStart = 0;
    size_t lastBreak = 0;

    for (size_t i = 0; i <= text.size(); i++) {
        bool isEnd = (i == text.size());
        bool isNewline = !isEnd && text[i] == '\n';
        bool isSpace = !isEnd && (text[i] == ' ' || text[i] == '\t');

        if (isSpace || isEnd || isNewline) {
            // Measure from lineStart to current position
            std::string segment = text.substr(lineStart, i - lineStart);
            Size segSize = ctx->measureText(segment, fontSize);

            if (segSize.width > maxWidth && lastBreak > lineStart) {
                // Wrap at last word break
                lines.push_back(text.substr(lineStart, lastBreak - lineStart));
                lineStart = lastBreak;
                // Skip whitespace
                while (lineStart < text.size() && text[lineStart] == ' ') lineStart++;
                i = lineStart;
                lastBreak = lineStart;
                continue;
            }

            if (isNewline) {
                lines.push_back(text.substr(lineStart, i - lineStart));
                lineStart = i + 1;
                lastBreak = lineStart;
                continue;
            }

            if (isEnd) {
                if (lineStart < text.size()) {
                    lines.push_back(text.substr(lineStart));
                }
            } else {
                lastBreak = i;
            }
        }
    }

    if (lines.empty()) lines.push_back(text);
    return lines;
}

static std::string getTruncatedText(const std::string& text, float maxWidth,
                                     float fontSize, GpuContext* ctx,
                                     const std::string& ellipsis = "...") {
    Size fullSize = ctx->measureText(text, fontSize);
    if (fullSize.width <= maxWidth) return text;

    Size ellipsisSize = ctx->measureText(ellipsis, fontSize);
    float availWidth = maxWidth - ellipsisSize.width;
    if (availWidth <= 0) return ellipsis;

    // Binary search for the right truncation point
    int lo = 0, hi = static_cast<int>(text.size());
    int best = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        std::string sub = text.substr(0, static_cast<size_t>(mid));
        Size subSize = ctx->measureText(sub, fontSize);
        if (subSize.width <= availWidth) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    return text.substr(0, static_cast<size_t>(best)) + ellipsis;
}

// ==================================================================
// Rendering
// ==================================================================

void Label::render(GpuContext* ctx) {
    if (!isVisible() || text_.empty()) return;

    // Resolve text color
    Color color = textColor_;
    if (color.a == 0) {
        Theme* t = currentTheme();
        color = t ? t->colors.textPrimary : Color::black();
    }

    // Disabled state
    if (!isEnabled()) {
        color = Color(color.r, color.g, color.b,
                      static_cast<uint8_t>(color.a / 2));
    }

    // Background
    if (backgroundColor_.a > 0) {
        ctx->fillRect(bounds_, backgroundColor_);
    }

    float contentX = bounds_.x + padding_.left;
    float contentY = bounds_.y + padding_.top;
    float contentW = bounds_.width - padding_.horizontal();
    float contentH = bounds_.height - padding_.vertical();

    if (wordWrap_) {
        // Multi-line rendering with word wrap
        std::vector<std::string> lines = breakLines(text_, contentW, fontSize_, ctx);
        float lineHeight = fontSize_ * lineSpacing_;
        float totalTextHeight = lines.size() * lineHeight;

        // Vertical centering within content area
        float startY = contentY;
        if (totalTextHeight < contentH) {
            startY = contentY + (contentH - totalTextHeight) / 2.0f;
        }

        // Clip rendering to prevent overflow
        for (size_t i = 0; i < lines.size(); i++) {
            float lineY = startY + i * lineHeight + fontSize_ * 0.8f;

            // Skip lines outside visible area
            if (lineY < bounds_.y) continue;
            if (lineY > bounds_.y + bounds_.height) break;

            float lineX = contentX;
            Size lineSize = ctx->measureText(lines[i], fontSize_);

            switch (alignment_) {
                case TextAlign::Center:
                    lineX = contentX + (contentW - lineSize.width) / 2.0f;
                    break;
                case TextAlign::Right:
                    lineX = contentX + contentW - lineSize.width;
                    break;
                default:
                    break;
            }

            ctx->drawText(lines[i], lineX, lineY, color, fontSize_);
        }
    } else {
        // Single-line rendering with optional truncation
        std::string displayText = text_;
        if (truncation_ != TextTruncation::None) {
            displayText = getTruncatedText(text_, contentW, fontSize_, ctx,
                                           truncation_ == TextTruncation::Middle ? "..." : "...");
        }

        Size textSize = ctx->measureText(displayText, fontSize_);

        // Horizontal alignment
        float textX = contentX;
        switch (alignment_) {
            case TextAlign::Center:
                textX = contentX + (contentW - textSize.width) / 2.0f;
                break;
            case TextAlign::Right:
                textX = contentX + contentW - textSize.width;
                break;
            default:
                break;
        }

        // Vertical centering
        float textY = contentY + (contentH - textSize.height) / 2.0f + textSize.height * 0.8f;

        ctx->drawText(displayText, textX, textY, color, fontSize_);
    }
}

// ==================================================================
// Measurement
// ==================================================================

Size Label::measure(const Size& available) {
    if (text_.empty()) {
        return Size(padding_.horizontal(), fontSize_ * lineSpacing_ + padding_.vertical());
    }

    // We need a GpuContext for text measurement. If unavailable, estimate.
    float charWidth = fontSize_ * 0.6f;

    if (wordWrap_ && available.width > 0) {
        // Estimate wrapped height
        float contentW = available.width - padding_.horizontal();
        if (contentW <= 0) contentW = 200;

        float charsPerLine = contentW / charWidth;
        if (charsPerLine < 1) charsPerLine = 1;
        int numLines = static_cast<int>(std::ceil(text_.size() / charsPerLine));
        numLines = std::max(1, numLines);

        return Size(
            available.width,
            numLines * fontSize_ * lineSpacing_ + padding_.vertical()
        );
    }

    // Single-line: approximate width
    float width = text_.size() * charWidth + padding_.horizontal();
    float height = fontSize_ * lineSpacing_ + padding_.vertical();

    if (maxWidth_ > 0 && width > maxWidth_) {
        width = maxWidth_;
    }

    return Size(width, height);
}

} // namespace NXRender
