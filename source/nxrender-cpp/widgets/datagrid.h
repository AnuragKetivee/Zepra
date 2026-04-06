// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "widgets/widget.h"
#include <vector>
#include <string>
#include <functional>

namespace NXRender {
namespace Widgets {

struct DataColumn {
    std::string title;
    float width = 100.0f;
    bool sortable = true;
};

// Abstract interface for providing virtualized data to the table
class RowProvider {
public:
    virtual ~RowProvider() = default;
    virtual size_t getRowCount() const = 0;
    virtual std::string getCellText(size_t rowIndex, size_t colIndex) const = 0;
    virtual Color getRowBackgroundColor(size_t rowIndex) const {
        return (rowIndex % 2 == 0) ? Color(250, 250, 250, 255) : Color::white();
    }
};

class DatagridWidget : public Widget {
public:
    DatagridWidget();
    ~DatagridWidget() override;

    void addColumn(const DataColumn& col);
    void setProvider(RowProvider* provider);
    
    // Virtualized UI styling
    void setRowHeight(float height) { rowHeight_ = height; }
    void setHeaderHeight(float height) { headerHeight_ = height; }

    // Core Drawing
    void render(GpuContext* ctx) override;

    // Event Interception (Input System Revamp Integration)
    EventResult handleRoutedEvent(const Input::Event& event) override;

private:
    std::vector<DataColumn> columns_;
    RowProvider* provider_ = nullptr;

    float rowHeight_ = 28.0f;
    float headerHeight_ = 32.0f;
    
    float scrollY_ = 0.0f;
    float scrollX_ = 0.0f;

    int activeSortColumn_ = -1;
    bool sortAscending_ = true;
    
    int hoveredRow_ = -1;
    int selectedRow_ = -1;

    // Hit test conversion
    int getRowIndexAt(float y) const;
};

} // namespace Widgets
} // namespace NXRender
