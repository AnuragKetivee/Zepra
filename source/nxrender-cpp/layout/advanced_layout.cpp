// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "advanced_layout.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <cstring>
#include <numeric>

namespace NXRender {

// ==================================================================
// MasonryLayout
// ==================================================================

std::vector<MasonryLayout::Item> MasonryLayout::compute(
    const std::vector<float>& itemHeights, int columns,
    float containerWidth, float gap) {

    if (columns <= 0 || containerWidth <= 0) return {};

    float colWidth = (containerWidth - gap * (columns - 1)) / columns;
    std::vector<float> columnHeights(columns, 0);
    std::vector<Item> items;
    items.reserve(itemHeights.size());

    for (size_t i = 0; i < itemHeights.size(); i++) {
        // Find shortest column
        int minCol = 0;
        float minHeight = columnHeights[0];
        for (int c = 1; c < columns; c++) {
            if (columnHeights[c] < minHeight) {
                minHeight = columnHeights[c];
                minCol = c;
            }
        }

        Item item;
        item.column = minCol;
        item.y = columnHeights[minCol];
        item.height = itemHeights[i];

        columnHeights[minCol] += itemHeights[i] + gap;
        items.push_back(item);
    }

    return items;
}

// ==================================================================
// AspectRatio
// ==================================================================

AspectRatio AspectRatio::parse(const std::string& str) {
    AspectRatio result;
    if (str.empty() || str == "auto") return result;

    result.isAuto = false;
    auto slash = str.find('/');
    if (slash != std::string::npos) {
        result.widthRatio = std::strtof(str.c_str(), nullptr);
        result.heightRatio = std::strtof(str.c_str() + slash + 1, nullptr);
    } else {
        result.widthRatio = std::strtof(str.c_str(), nullptr);
        result.heightRatio = 1;
    }
    return result;
}

// ==================================================================
// LogicalProperties
// ==================================================================

float LogicalProperties::inlineStart(float left, float right) const {
    if (isHorizontal()) {
        return (direction == Direction::LTR) ? left : right;
    }
    return left; // Simplified
}

float LogicalProperties::inlineEnd(float left, float right) const {
    if (isHorizontal()) {
        return (direction == Direction::LTR) ? right : left;
    }
    return right;
}

float LogicalProperties::blockStart(float top, float bottom) const {
    if (isHorizontal()) return top;
    return (writingMode == WritingMode::VerticalRL) ? top : bottom;
}

float LogicalProperties::blockEnd(float top, float bottom) const {
    if (isHorizontal()) return bottom;
    return (writingMode == WritingMode::VerticalRL) ? bottom : top;
}

float LogicalProperties::inlineSize(float width, float height) const {
    return isHorizontal() ? width : height;
}

float LogicalProperties::blockSize(float width, float height) const {
    return isHorizontal() ? height : width;
}

// ==================================================================
// GapValue
// ==================================================================

GapValue GapValue::parse(const std::string& str) {
    GapValue result;
    if (str == "normal") return result;

    std::istringstream ss(str);
    std::string token;

    if (ss >> token) {
        if (token != "normal") {
            result.rowGap = std::strtof(token.c_str(), nullptr);
            result.rowGapNormal = false;
        }
    }
    if (ss >> token) {
        if (token != "normal") {
            result.columnGap = std::strtof(token.c_str(), nullptr);
            result.columnGapNormal = false;
        }
    } else {
        result.columnGap = result.rowGap;
        result.columnGapNormal = result.rowGapNormal;
    }

    return result;
}

// ==================================================================
// PlaceValue
// ==================================================================

static AlignValue parseAlignKeyword(const std::string& s) {
    if (s == "center") return AlignValue::Center;
    if (s == "start") return AlignValue::Start;
    if (s == "end") return AlignValue::End;
    if (s == "flex-start") return AlignValue::FlexStart;
    if (s == "flex-end") return AlignValue::FlexEnd;
    if (s == "stretch") return AlignValue::Stretch;
    if (s == "baseline") return AlignValue::Baseline;
    if (s == "space-between") return AlignValue::SpaceBetween;
    if (s == "space-around") return AlignValue::SpaceAround;
    if (s == "space-evenly") return AlignValue::SpaceEvenly;
    if (s == "self-start") return AlignValue::SelfStart;
    if (s == "self-end") return AlignValue::SelfEnd;
    if (s == "normal") return AlignValue::Normal;
    return AlignValue::Auto;
}

PlaceValue PlaceValue::parse(const std::string& str) {
    PlaceValue result;
    std::istringstream ss(str);
    std::string token;

    if (ss >> token) result.align = parseAlignKeyword(token);
    if (ss >> token) result.justify = parseAlignKeyword(token);
    else result.justify = result.align;

    return result;
}

// ==================================================================
// CSSShape
// ==================================================================

CSSShape CSSShape::parse(const std::string& str) {
    CSSShape shape;
    if (str.empty() || str == "none") return shape;

    // circle(radius at cx cy)
    if (str.find("circle(") == 0) {
        shape.type = Type::Circle;
        auto inner = str.substr(7, str.size() - 8);
        auto at = inner.find("at");
        if (at != std::string::npos) {
            shape.circleRadius = std::strtof(inner.c_str(), nullptr);
            std::string pos = inner.substr(at + 3);
            std::istringstream ss(pos);
            std::string cx, cy;
            if (ss >> cx) shape.circleCenterX = std::strtof(cx.c_str(), nullptr) / 100.0f;
            if (ss >> cy) shape.circleCenterY = std::strtof(cy.c_str(), nullptr) / 100.0f;
        } else {
            shape.circleRadius = std::strtof(inner.c_str(), nullptr);
        }
        return shape;
    }

    // ellipse(rx ry at cx cy)
    if (str.find("ellipse(") == 0) {
        shape.type = Type::Ellipse;
        auto inner = str.substr(8, str.size() - 9);
        std::istringstream ss(inner);
        std::string rx, ry;
        if (ss >> rx) shape.ellipseRadiusX = std::strtof(rx.c_str(), nullptr);
        if (ss >> ry) shape.ellipseRadiusY = std::strtof(ry.c_str(), nullptr);
        return shape;
    }

    // inset(top right bottom left round radius)
    if (str.find("inset(") == 0) {
        shape.type = Type::Inset;
        auto inner = str.substr(6, str.size() - 7);
        auto round = inner.find("round");
        std::string insets = (round != std::string::npos) ? inner.substr(0, round) : inner;
        std::istringstream ss(insets);
        std::string v;
        std::vector<float> vals;
        while (ss >> v) vals.push_back(std::strtof(v.c_str(), nullptr));
        if (vals.size() >= 1) shape.insetTop = vals[0];
        if (vals.size() >= 2) shape.insetRight = vals[1]; else shape.insetRight = shape.insetTop;
        if (vals.size() >= 3) shape.insetBottom = vals[2]; else shape.insetBottom = shape.insetTop;
        if (vals.size() >= 4) shape.insetLeft = vals[3]; else shape.insetLeft = shape.insetRight;

        if (round != std::string::npos) {
            auto radiusStr = inner.substr(round + 6);
            shape.insetBorderRadius = std::strtof(radiusStr.c_str(), nullptr);
        }
        return shape;
    }

    // polygon(x1 y1, x2 y2, ...)
    if (str.find("polygon(") == 0) {
        shape.type = Type::Polygon;
        auto inner = str.substr(8, str.size() - 9);

        // Check fill rule
        if (inner.find("evenodd") == 0) {
            shape.fillRule = CSSShape::FillRule::EvenOdd;
            inner = inner.substr(inner.find(',') + 1);
        } else if (inner.find("nonzero") == 0) {
            inner = inner.substr(inner.find(',') + 1);
        }

        std::istringstream ss(inner);
        std::string pair;
        while (std::getline(ss, pair, ',')) {
            std::istringstream ps(pair);
            std::string x, y;
            if (ps >> x >> y) {
                float xv = std::strtof(x.c_str(), nullptr);
                float yv = std::strtof(y.c_str(), nullptr);
                // Convert % if needed
                if (x.find('%') != std::string::npos) xv *= 0.01f;
                if (y.find('%') != std::string::npos) yv *= 0.01f;
                shape.polygonPoints.push_back({xv, yv});
            }
        }
        return shape;
    }

    // path("M ...")
    if (str.find("path(") == 0) {
        shape.type = Type::Path;
        auto start = str.find('"');
        auto end = str.rfind('"');
        if (start != std::string::npos && end > start) {
            shape.pathData = str.substr(start + 1, end - start - 1);
        }
        return shape;
    }

    // url(#id)
    if (str.find("url(") == 0) {
        shape.type = Type::URL;
        auto hash = str.find('#');
        auto close = str.rfind(')');
        if (hash != std::string::npos && close > hash) {
            shape.url = str.substr(hash + 1, close - hash - 1);
        }
        return shape;
    }

    return shape;
}

bool CSSShape::contains(float x, float y, float boxWidth, float boxHeight) const {
    switch (type) {
        case Type::None: return true;
        case Type::Inset: {
            float left = insetLeft;
            float top = insetTop;
            float right = boxWidth - insetRight;
            float bottom = boxHeight - insetBottom;
            return x >= left && x <= right && y >= top && y <= bottom;
        }
        case Type::Circle: {
            float cx = circleCenterX * boxWidth;
            float cy = circleCenterY * boxHeight;
            float r = (circleRadius > 0) ? circleRadius :
                       std::min(boxWidth, boxHeight) / 2;
            float dx = x - cx, dy = y - cy;
            return (dx * dx + dy * dy) <= (r + shapeMargin) * (r + shapeMargin);
        }
        case Type::Ellipse: {
            float cx = ellipseCenterX * boxWidth;
            float cy = ellipseCenterY * boxHeight;
            float rx = (ellipseRadiusX > 0) ? ellipseRadiusX : boxWidth / 2;
            float ry = (ellipseRadiusY > 0) ? ellipseRadiusY : boxHeight / 2;
            float dx = (x - cx) / rx, dy = (y - cy) / ry;
            return (dx * dx + dy * dy) <= 1;
        }
        case Type::Polygon: {
            // Ray casting
            if (polygonPoints.size() < 3) return false;
            bool inside = false;
            size_t n = polygonPoints.size();
            for (size_t i = 0, j = n - 1; i < n; j = i++) {
                float xi = polygonPoints[i].x * boxWidth;
                float yi = polygonPoints[i].y * boxHeight;
                float xj = polygonPoints[j].x * boxWidth;
                float yj = polygonPoints[j].y * boxHeight;
                if (((yi > y) != (yj > y)) &&
                    (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
                    inside = !inside;
                }
            }
            return inside;
        }
        default:
            return true;
    }
}

// ==================================================================
// ScrollTimeline
// ==================================================================

float ScrollTimeline::computeProgress(float scrollPosition, float scrollMax) const {
    if (scrollMax <= 0) return 0;
    float raw = scrollPosition / scrollMax;
    float range = rangeEnd - rangeStart;
    if (range <= 0) return 0;
    return std::clamp((raw - rangeStart) / range, 0.0f, 1.0f);
}

// ==================================================================
// ViewTimeline
// ==================================================================

float ViewTimeline::computeProgress(float elementPosition, float elementSize,
                                       float viewportSize, float scrollPosition) const {
    // Element enters viewport when scrollPosition reaches (elementPosition - viewportSize)
    // Element leaves when scrollPosition reaches (elementPosition + elementSize)
    float enterEdge = elementPosition - viewportSize;
    float exitEdge = elementPosition + elementSize;
    float totalRange = exitEdge - enterEdge;
    if (totalRange <= 0) return 0;
    return std::clamp((scrollPosition - enterEdge) / totalRange, 0.0f, 1.0f);
}

// ==================================================================
// AnchorPosition
// ==================================================================

float AnchorPosition::resolve(float anchorPos, float anchorSize, float selfSize) const {
    switch (edge) {
        case Edge::Top: return anchorPos;
        case Edge::Bottom: return anchorPos + anchorSize;
        case Edge::Left: return anchorPos;
        case Edge::Right: return anchorPos + anchorSize;
        case Edge::Center: return anchorPos + anchorSize / 2 - selfSize / 2;
    }
    return anchorPos;
}

// ==================================================================
// ContainerStyleQuery
// ==================================================================

bool ContainerStyleQuery::evaluate(
    const std::function<std::string(const std::string&)>& resolver) const {
    for (const auto& cond : conditions) {
        std::string actual = resolver(cond.property);
        switch (cond.op) {
            case Condition::Op::Equals:
                if (actual != cond.value) return false;
                break;
            case Condition::Op::Contains:
                if (actual.find(cond.value) == std::string::npos) return false;
                break;
        }
    }
    return true;
}

// ==================================================================
// AdvancedLayout
// ==================================================================

float AdvancedLayout::minContentWidth(const std::vector<float>& childWidths) {
    float maxChild = 0;
    for (float w : childWidths) maxChild = std::max(maxChild, w);
    return maxChild;
}

float AdvancedLayout::maxContentWidth(const std::vector<float>& childWidths) {
    float total = 0;
    for (float w : childWidths) total += w;
    return total;
}

float AdvancedLayout::fitContentWidth(const std::vector<float>& childWidths, float available) {
    float minContent = minContentWidth(childWidths);
    float maxContent = maxContentWidth(childWidths);
    return std::max(minContent, std::min(maxContent, available));
}

void AdvancedLayout::distributeAutoMargins(float containerSize, float contentSize,
                                              bool autoStart, bool autoEnd,
                                              float& marginStart, float& marginEnd) {
    float freeSpace = containerSize - contentSize;
    if (freeSpace <= 0) {
        marginStart = marginEnd = 0;
        return;
    }
    if (autoStart && autoEnd) {
        marginStart = marginEnd = freeSpace / 2;
    } else if (autoStart) {
        marginStart = freeSpace;
        marginEnd = 0;
    } else if (autoEnd) {
        marginStart = 0;
        marginEnd = freeSpace;
    }
}

float AdvancedLayout::computeBaseline(const std::vector<float>& baselines,
                                         const std::vector<float>& heights) {
    if (baselines.empty()) return 0;
    float maxBaseline = 0;
    for (size_t i = 0; i < baselines.size(); i++) {
        maxBaseline = std::max(maxBaseline, baselines[i]);
    }
    return maxBaseline;
}

std::vector<float> AdvancedLayout::computeFragmentBreaks(
    const std::vector<float>& childHeights,
    const FragmentationContext& ctx) {

    std::vector<float> breaks;
    if (ctx.fragmentainerHeight <= 0) return breaks;

    float currentHeight = 0;

    for (size_t i = 0; i < childHeights.size(); i++) {
        float childH = childHeights[i];

        if (currentHeight + childH > ctx.fragmentainerHeight) {
            // Break needed
            if (ctx.breakInside == BreakValue::Avoid && i > 0) {
                // Try to avoid breaking inside — push to next fragmentainer
                breaks.push_back(currentHeight);
                currentHeight = childH;
            } else {
                breaks.push_back(ctx.fragmentainerHeight);
                currentHeight = childH - (ctx.fragmentainerHeight - currentHeight);
                while (currentHeight > ctx.fragmentainerHeight) {
                    breaks.push_back(ctx.fragmentainerHeight);
                    currentHeight -= ctx.fragmentainerHeight;
                }
            }
        } else {
            currentHeight += childH;
        }
    }

    return breaks;
}

} // namespace NXRender
