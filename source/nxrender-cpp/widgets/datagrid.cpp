// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "datagrid.h"
#include "nxgfx/context.h"
#include <algorithm>
#include <cmath>

namespace NXRender {
namespace Widgets {

DatagridWidget::DatagridWidget() {
    backgroundColor_ = Color::white();
}

DatagridWidget::~DatagridWidget() {}

void DatagridWidget::addColumn(const DataColumn& col) {
    columns_.push_back(col);
}

void DatagridWidget::setProvider(RowProvider* provider) {
    provider_ = provider;
    scrollY_ = 0.0f; // Reset scroll on data change
}

int DatagridWidget::getRowIndexAt(float localY) const {
    if (localY <= headerHeight_) return -1;
    float dataY = localY - headerHeight_ + scrollY_;
    int index = static_cast<int>(std::floor(dataY / rowHeight_));
    if (provider_ && index >= static_cast<int>(provider_->getRowCount())) return -1;
    return index;
}

EventResult DatagridWidget::handleRoutedEvent(const Input::Event& event) {
    if (!state_.enabled) return EventResult::Ignored;
    
    // Leverage the new Event System
    if (auto scrollEv = dynamic_cast<const Input::ScrollEvent*>(&event)) {
        scrollY_ += scrollEv->deltaY() * 20.0f; // Basic scroll multiplier
        scrollX_ += scrollEv->deltaX() * 20.0f;

        if (scrollY_ < 0) scrollY_ = 0;
        
        float maxScrollY = 0;
        if (provider_) {
            maxScrollY = std::max(0.0f, (provider_->getRowCount() * rowHeight_) - (bounds_.height - headerHeight_));
        }
        if (scrollY_ > maxScrollY) scrollY_ = maxScrollY;
        
        if (scrollX_ < 0) scrollX_ = 0;

        return EventResult::NeedsRedraw;
    }
    
    if (auto mouseEv = dynamic_cast<const Input::MouseEvent*>(&event)) {
        float lx = mouseEv->x() - bounds_.x;
        float ly = mouseEv->y() - bounds_.y;

        if (mouseEv->type() == Input::EventType::MouseMove) {
            int newHover = getRowIndexAt(ly);
            if (newHover != hoveredRow_) {
                hoveredRow_ = newHover;
                return EventResult::NeedsRedraw;
            }
        } else if (mouseEv->type() == Input::EventType::MouseLeave) {
            hoveredRow_ = -1;
            return EventResult::NeedsRedraw;
        } else if (mouseEv->type() == Input::EventType::MouseDown && mouseEv->button() == 0) {
            int clickedRow = getRowIndexAt(ly);
            if (clickedRow >= 0) {
                selectedRow_ = clickedRow;
                // Dispatch internal selection event here using standard routing if connected
                return EventResult::NeedsRedraw;
            }
            // Header click for sorting
            if (ly <= headerHeight_) {
                float curX = -scrollX_;
                for (size_t i = 0; i < columns_.size(); ++i) {
                    if (lx >= curX && lx <= curX + columns_[i].width) {
                        if (activeSortColumn_ == static_cast<int>(i)) {
                            sortAscending_ = !sortAscending_;
                        } else {
                            activeSortColumn_ = static_cast<int>(i);
                            sortAscending_ = true;
                        }
                        return EventResult::NeedsRedraw;
                    }
                    curX += columns_[i].width;
                }
            }
        }
    }

    if (auto keyEv = dynamic_cast<const Input::KeyEvent*>(&event)) {
        if (keyEv->type() == Input::EventType::KeyDown && provider_) {
            size_t maxIdx = provider_->getRowCount();
            if (maxIdx > 0) {
                if (keyEv->keyString() == "ArrowDown") {
                    selectedRow_ = std::min(selectedRow_ + 1, static_cast<int>(maxIdx - 1));
                    return EventResult::NeedsRedraw;
                } else if (keyEv->keyString() == "ArrowUp") {
                    selectedRow_ = std::max(selectedRow_ - 1, 0);
                    return EventResult::NeedsRedraw;
                }
            }
        }
    }

    return Widget::handleRoutedEvent(event);
}

void DatagridWidget::render(GpuContext* ctx) {
    if (!state_.visible) return;

    // Draw background
    ctx->fillRect(bounds_, backgroundColor_);

    // Setup viewport clipping for scroll boundary constraints
    ctx->pushClip(bounds_);

    // 1. Draw Data Rows (Virtualized rendering!)
    if (provider_) {
        size_t totalRows = provider_->getRowCount();
        
        // Exact hardware rendering calculation: Only draw visible rows natively!
        size_t startRow = static_cast<size_t>(std::max(0.0f, std::floor(scrollY_ / rowHeight_)));
        size_t visibleCount = static_cast<size_t>(std::ceil((bounds_.height - headerHeight_) / rowHeight_)) + 1;
        size_t endRow = std::min(startRow + visibleCount, totalRows);

        for (size_t r = startRow; r < endRow; ++r) {
            float rowY = bounds_.y + headerHeight_ + (r * rowHeight_) - scrollY_;
            Rect rowRect(bounds_.x, rowY, bounds_.width, rowHeight_);
            
            // Interaction States
            Color rowBg = provider_->getRowBackgroundColor(r);
            if (static_cast<int>(r) == selectedRow_) {
                rowBg = Color(200, 225, 250, 255); // Native OS accent selection
            } else if (static_cast<int>(r) == hoveredRow_) {
                rowBg = Color(230, 240, 245, 255);
            }
            
            ctx->fillRect(rowRect, rowBg);
            ctx->fillRect(Rect(rowRect.x, rowRect.y + rowRect.height - 1, rowRect.width, 1), Color(235, 235, 235, 255)); // Separator line

            float curX = bounds_.x - scrollX_;
            for (size_t c = 0; c < columns_.size(); ++c) {
                // Clipping at cell bounds
                Rect cellRect(curX + 8, rowY + 4, columns_[c].width - 16, rowHeight_ - 8);
                // Draw actual cell text using production rendering context
                ctx->drawText(provider_->getCellText(r, c), cellRect.x, cellRect.y + 14, Color::black(), 14.0f);
                curX += columns_[c].width;
            }
        }
    }

    // 2. Draw Header
    Rect headerRect(bounds_.x, bounds_.y, bounds_.width, headerHeight_);
    ctx->fillRect(headerRect, Color(245, 245, 245, 255));
    ctx->fillRect(Rect(headerRect.x, headerRect.y + headerRect.height - 1, headerRect.width, 1), Color(200, 200, 200, 255));

    float curX = bounds_.x - scrollX_;
    for (size_t i = 0; i < columns_.size(); ++i) {
        // Draw column separator
        if (i > 0) {
            ctx->fillRect(Rect(curX, bounds_.y + 4, 1, headerHeight_ - 8), Color(220, 220, 220, 255));
        }
        
        // Draw real Header Title text
        ctx->drawText(columns_[i].title, curX + 8, bounds_.y + headerHeight_ - 8, Color::black(), 14.0f);
        
        curX += columns_[i].width;
    }

    ctx->popClip();
    
    // Draw outer boundary
    ctx->strokeRect(bounds_, Color(200, 200, 200, 255), 1.0f);
    
    Widget::renderChildren(ctx); // Process normal child rendering
}

} // namespace Widgets
} // namespace NXRender
