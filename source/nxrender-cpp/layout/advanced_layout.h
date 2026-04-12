// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "nxgfx/primitives.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>

namespace NXRender {

// ==================================================================
// CSS Subgrid — grid items inheriting parent track definitions
// ==================================================================

struct SubgridContext {
    bool subgridRows = false;
    bool subgridColumns = false;

    // Inherited tracks from parent grid
    std::vector<float> parentRowTracks;
    std::vector<float> parentColumnTracks;

    // Line name mappings
    std::vector<std::string> parentRowLineNames;
    std::vector<std::string> parentColumnLineNames;

    // Start position in parent grid
    int parentRowStart = 0;
    int parentColumnStart = 0;
    int parentRowSpan = 1;
    int parentColumnSpan = 1;
};

// ==================================================================
// CSS Masonry Layout
// ==================================================================

enum class MasonryDirection : uint8_t {
    Row, Column
};

struct MasonryLayout {
    MasonryDirection direction = MasonryDirection::Column;
    float gap = 0;
    int columns = 0;      // 0 = auto-fill
    float columnWidth = 0; // For auto-fill

    struct Item {
        int column = 0;
        float y = 0;
        float height = 0;
    };

    // Compute item positions
    static std::vector<Item> compute(const std::vector<float>& itemHeights,
                                      int columns, float containerWidth,
                                      float gap);
};

// ==================================================================
// Aspect Ratio — aspect-ratio: width / height
// ==================================================================

struct AspectRatio {
    float widthRatio = 0;
    float heightRatio = 0;
    bool isAuto = true;

    float value() const {
        return (heightRatio > 0) ? widthRatio / heightRatio : 0;
    }

    float computeHeight(float width) const {
        float r = value();
        return (r > 0) ? width / r : 0;
    }

    float computeWidth(float height) const {
        return height * value();
    }

    static AspectRatio parse(const std::string& str);
};

// ==================================================================
// CSS Logical Properties
// ==================================================================

enum class WritingMode : uint8_t {
    HorizontalTB, VerticalRL, VerticalLR, SidewaysRL, SidewaysLR
};

enum class Direction : uint8_t {
    LTR, RTL
};

struct LogicalProperties {
    WritingMode writingMode = WritingMode::HorizontalTB;
    Direction direction = Direction::LTR;

    // Convert logical → physical
    float inlineStart(float left, float right) const;
    float inlineEnd(float left, float right) const;
    float blockStart(float top, float bottom) const;
    float blockEnd(float top, float bottom) const;

    // Size
    float inlineSize(float width, float height) const;
    float blockSize(float width, float height) const;

    // Is horizontal?
    bool isHorizontal() const {
        return writingMode == WritingMode::HorizontalTB;
    }
};

// ==================================================================
// CSS Gap / Row-Gap / Column-Gap
// ==================================================================

struct GapValue {
    float rowGap = 0;
    float columnGap = 0;
    bool rowGapNormal = true;  // normal = UA default
    bool columnGapNormal = true;

    static GapValue parse(const std::string& str);
};

// ==================================================================
// CSS place-items / place-content / place-self shorthand
// ==================================================================

enum class AlignValue : uint8_t {
    Auto, Normal, Stretch, Center, Start, End,
    FlexStart, FlexEnd, SelfStart, SelfEnd,
    Baseline, FirstBaseline, LastBaseline,
    SpaceBetween, SpaceAround, SpaceEvenly
};

struct PlaceValue {
    AlignValue align = AlignValue::Auto;  // block axis
    AlignValue justify = AlignValue::Auto; // inline axis

    static PlaceValue parse(const std::string& str);
};

// ==================================================================
// CSS Shapes — shape-outside, clip-path
// ==================================================================

struct CSSShape {
    enum class Type : uint8_t {
        None, Inset, Circle, Ellipse, Polygon, Path, URL
    } type = Type::None;

    // Inset
    float insetTop = 0, insetRight = 0, insetBottom = 0, insetLeft = 0;
    float insetBorderRadius = 0;

    // Circle
    float circleRadius = 0; // 0 = closest-side
    float circleCenterX = 0.5f; // 0..1 relative
    float circleCenterY = 0.5f;
    enum class SizeKeyword { ClosestSide, FarthestSide, ClosestCorner, FarthestCorner } sizeKeyword;

    // Ellipse
    float ellipseRadiusX = 0, ellipseRadiusY = 0;
    float ellipseCenterX = 0.5f, ellipseCenterY = 0.5f;

    // Polygon
    struct PolygonPoint { float x, y; };
    std::vector<PolygonPoint> polygonPoints;
    enum class FillRule : uint8_t { NonZero, EvenOdd } fillRule = FillRule::NonZero;

    // Path
    std::string pathData; // SVG path d attribute

    // URL
    std::string url;

    // Reference box
    enum class ReferenceBox : uint8_t {
        MarginBox, BorderBox, PaddingBox, ContentBox, FillBox, StrokeBox, ViewBox
    } referenceBox = ReferenceBox::BorderBox;

    static CSSShape parse(const std::string& str);

    // Hit testing
    bool contains(float x, float y, float boxWidth, float boxHeight) const;

    // Shape margin
    float shapeMargin = 0;

    // Image threshold (for shape-outside: url())
    float shapeImageThreshold = 0;
};

// ==================================================================
// CSS Scroll Timeline
// ==================================================================

struct ScrollTimeline {
    std::string name;
    enum class Source : uint8_t { Nearest, Root, Self } source = Source::Nearest;
    enum class Axis : uint8_t { Block, Inline, X, Y } axis = Axis::Block;

    // Scroll range
    float rangeStart = 0; // 0..1 (percentage of scroll range)
    float rangeEnd = 1;

    float computeProgress(float scrollPosition, float scrollMax) const;
};

struct ViewTimeline {
    std::string name;
    enum class Axis : uint8_t { Block, Inline, X, Y } axis = Axis::Block;
    std::string inset; // "auto" or length

    float computeProgress(float elementPosition, float elementSize,
                            float viewportSize, float scrollPosition) const;
};

// ==================================================================
// CSS Anchor Positioning
// ==================================================================

struct AnchorPosition {
    std::string anchorName;  // anchor-name: --name

    // Position functions: anchor(--name top/bottom/left/right)
    enum class Edge : uint8_t { Top, Right, Bottom, Left, Center } edge;
    std::string fallback; // position-fallback

    float resolve(float anchorPos, float anchorSize, float selfSize) const;
};

struct PositionFallback {
    std::string name;
    struct TryBlock {
        std::string positionArea; // "top", "bottom", "left", "right"
        float insetTop = 0, insetRight = 0, insetBottom = 0, insetLeft = 0;
        AlignValue alignSelf = AlignValue::Auto;
        AlignValue justifySelf = AlignValue::Auto;
    };
    std::vector<TryBlock> tries;
};

// ==================================================================
// CSS Container Style Queries
// ==================================================================

struct ContainerStyleQuery {
    std::string containerName;
    struct Condition {
        std::string property;
        std::string value;
        enum class Op { Equals, Contains } op = Op::Equals;
    };
    std::vector<Condition> conditions;

    bool evaluate(const std::function<std::string(const std::string&)>& resolver) const;
};

// ==================================================================
// Advanced Layout Computations
// ==================================================================

class AdvancedLayout {
public:
    // Intrinsic sizing: min-content / max-content / fit-content
    static float minContentWidth(const std::vector<float>& childWidths);
    static float maxContentWidth(const std::vector<float>& childWidths);
    static float fitContentWidth(const std::vector<float>& childWidths, float available);

    // Auto margins distribution
    static void distributeAutoMargins(float containerSize, float contentSize,
                                        bool autoStart, bool autoEnd,
                                        float& marginStart, float& marginEnd);

    // Baseline alignment
    static float computeBaseline(const std::vector<float>& baselines,
                                   const std::vector<float>& heights);

    // Fragmentation (CSS break-before/after/inside)
    enum class BreakValue : uint8_t {
        Auto, Avoid, Always, All, Page, Column, Region,
        Left, Right, Recto, Verso, AvoidPage, AvoidColumn, AvoidRegion
    };

    struct FragmentationContext {
        float fragmentainerHeight = 0;
        BreakValue breakBefore = BreakValue::Auto;
        BreakValue breakAfter = BreakValue::Auto;
        BreakValue breakInside = BreakValue::Auto;
        int orphans = 2;
        int widows = 2;
    };

    // Compute fragment breaks
    static std::vector<float> computeFragmentBreaks(
        const std::vector<float>& childHeights,
        const FragmentationContext& ctx);
};

} // namespace NXRender
