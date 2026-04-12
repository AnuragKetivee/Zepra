// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "web/box/box_tree.h"
#include "web/paint/paint_ops.h"
#include "web/observers.h"
#include "core/render_pipeline.h"
#include "core/display_list.h"
#include "nxgfx/primitives.h"
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace NXRender {
namespace Web {

class PaintTreeBuilder;

// ==================================================================
// Frame orchestrator — the single entry point for rendering a frame
//
// Coordinates: dirty check → style recalc → layout → paint → composite
//
// This is the heartbeat of the rendering engine. Each call to
// renderFrame() produces one composited frame.
// ==================================================================

class FrameOrchestrator {
public:
    FrameOrchestrator();
    ~FrameOrchestrator();

    // Attach root box tree
    void setRootBox(BoxNode* root) { root_ = root; }
    BoxNode* rootBox() const { return root_; }

    // Attach GPU pipeline
    void setRenderPipeline(RenderPipeline* pipeline) { pipeline_ = pipeline; }

    // Viewport
    void setViewport(float width, float height, float dpr = 1.0f);
    float viewportWidth() const { return viewportWidth_; }
    float viewportHeight() const { return viewportHeight_; }
    float devicePixelRatio() const { return dpr_; }

    // Scroll position
    void setScrollOffset(float x, float y);
    float scrollX() const { return scrollX_; }
    float scrollY() const { return scrollY_; }

    // ==================================================================
    // Frame lifecycle
    // ==================================================================

    // Full frame: style → layout → paint → composite
    void renderFrame();

    // Partial updates
    void scheduleStyleRecalc();
    void scheduleLayout();
    void schedulePaint();

    // Force synchronous
    void forceStyleRecalc();
    void forceLayout();

    // ==================================================================
    // Dirty tracking
    // ==================================================================

    void markStyleDirty(BoxNode* node);
    void markLayoutDirty(BoxNode* node);
    void markPaintDirty(BoxNode* node);
    void markSubtreeDirty(BoxNode* node);

    bool needsStyleRecalc() const { return needsStyleRecalc_; }
    bool needsLayout() const { return needsLayout_; }
    bool needsPaint() const { return needsPaint_; }

    // ==================================================================
    // Frame stats
    // ==================================================================

    struct FrameStats {
        double styleRecalcMs = 0;
        double layoutMs = 0;
        double paintMs = 0;
        double compositeMs = 0;
        double totalMs = 0;
        int layoutCount = 0;     // number of boxes laid out
        int paintOpCount = 0;    // number of paint ops generated
        int layerCount = 0;
        bool fullLayout = false;
        bool fullPaint = false;
    };
    const FrameStats& lastFrameStats() const { return lastStats_; }

    // ==================================================================
    // Callbacks
    // ==================================================================

    using FrameCallback = std::function<void(double timestamp)>;
    int requestAnimationFrame(FrameCallback cb);
    void cancelAnimationFrame(int id);

    // Post-layout callback (e.g., for IntersectionObserver)
    using PostLayoutCallback = std::function<void()>;
    void addPostLayoutCallback(PostLayoutCallback cb);

private:
    BoxNode* root_ = nullptr;
    RenderPipeline* pipeline_ = nullptr;

    float viewportWidth_ = 0;
    float viewportHeight_ = 0;
    float dpr_ = 1.0f;
    float scrollX_ = 0;
    float scrollY_ = 0;

    bool needsStyleRecalc_ = true;
    bool needsLayout_ = true;
    bool needsPaint_ = true;

    FrameStats lastStats_;

    // RAF callbacks
    struct RAFEntry {
        int id;
        FrameCallback callback;
    };
    std::vector<RAFEntry> rafCallbacks_;
    int nextRAFId_ = 1;

    // Post-layout hooks
    std::vector<PostLayoutCallback> postLayoutCallbacks_;

    // Internal phases
    void runStyleRecalc();
    void runLayout();
    void runPaint();
    void runComposite();
    void runRAFCallbacks(double timestamp);
    void runPostLayoutCallbacks();
};

// ==================================================================
// Layout Engine — the actual layout solver
//
// Given a box tree with computed styles, resolves concrete positions
// and dimensions for every box. This is where CSS layout algorithms
// execute: block flow, inline flow, flex, grid, positioned, floats.
// ==================================================================

class LayoutEngine {
public:
    LayoutEngine();
    ~LayoutEngine();

    struct LayoutContext {
        float containingBlockWidth = 0;
        float containingBlockHeight = 0;
        float viewportWidth = 0;
        float viewportHeight = 0;
        float dpr = 1.0f;

        // BFC (Block Formatting Context) state
        float currentY = 0;
        float pendingMargin = 0;          // For margin collapsing
        bool atBFCStart = true;           // No content placed yet

        // Float state
        struct FloatRect {
            float x, y, width, height;
            bool isLeft;
        };
        std::vector<FloatRect> floats;

        // Available space accounting with floats
        float availableWidthAt(float y, float blockWidth) const;
        float leftEdgeAt(float y) const;
        float rightEdgeAt(float y, float blockWidth) const;
        float clearance(bool left, bool right) const;
    };

    // Run layout on the full tree
    int layoutTree(BoxNode* root, float viewportWidth, float viewportHeight, float dpr = 1.0f);

    // Layout a subtree (incremental)
    int layoutSubtree(BoxNode* node, const LayoutContext& parentCtx);

private:
    int layoutCount_ = 0;

    // Dispatch to correct layout mode
    void layoutNode(BoxNode* node, LayoutContext& ctx);

    // Block formatting context
    void layoutBlock(BoxNode* node, LayoutContext& ctx);
    void layoutBlockChildren(BoxNode* node, LayoutContext& ctx);

    // Inline formatting context
    void layoutInline(BoxNode* node, LayoutContext& ctx);
    void layoutInlineChildren(BoxNode* node, LayoutContext& ctx);

    // Flex layout
    void layoutFlex(BoxNode* node, LayoutContext& ctx);

    // Grid layout
    void layoutGrid(BoxNode* node, LayoutContext& ctx);

    // Table layout
    void layoutTable(BoxNode* node, LayoutContext& ctx);

    // Positioned elements (absolute/fixed/sticky)
    void layoutPositioned(BoxNode* node, LayoutContext& ctx);

    // Float placement
    void placeFloat(BoxNode* node, LayoutContext& ctx);

    // Resolve CSS length values to pixels
    float resolveLength(const std::string& value, float referenceSize,
                          float viewportWidth, float viewportHeight) const;
    float resolveWidth(BoxNode* node, const LayoutContext& ctx) const;
    float resolveHeight(BoxNode* node, const LayoutContext& ctx) const;
    float resolveMinWidth(BoxNode* node, const LayoutContext& ctx) const;
    float resolveMaxWidth(BoxNode* node, const LayoutContext& ctx) const;
    float resolveMinHeight(BoxNode* node, const LayoutContext& ctx) const;
    float resolveMaxHeight(BoxNode* node, const LayoutContext& ctx) const;

    // Margin collapse
    float collapseMargins(float marginA, float marginB) const;

    // Shrink-to-fit width for floats and inline-blocks
    float shrinkToFitWidth(BoxNode* node, const LayoutContext& ctx);
};

// ==================================================================
// Inline Layout Engine — line box generation and text wrapping
//
// Handles: line breaking, word wrapping, white-space processing,
// text-align, vertical-align, BiDi, inline replaced elements.
// ==================================================================

class InlineLayoutEngine {
public:
    struct InlineItem {
        enum class Type : uint8_t {
            Text, InlineBox, InlineBlockBox, ReplacedElement,
            LineBreak, WhiteSpace
        } type;

        BoxNode* node = nullptr;
        std::string text;

        // Measured dimensions
        float width = 0;
        float height = 0;
        float baseline = 0;     // distance from top to baseline

        // Fragment tracking (for wrapped text)
        size_t textStartIndex = 0;
        size_t textEndIndex = 0;

        // White-space
        bool breakable = true;
        bool collapsible = true;
    };

    struct LineBox {
        float x = 0, y = 0;
        float width = 0;        // used width
        float height = 0;
        float baseline = 0;     // baseline relative to top of line
        float maxAscent = 0;
        float maxDescent = 0;
        float availableWidth = 0;
        std::vector<InlineItem> items;
    };

    // Break inline content into line boxes
    std::vector<LineBox> layout(BoxNode* inlineContainer,
                                  float availableWidth, float startY);

private:
    // Break text into items (respecting white-space property)
    std::vector<InlineItem> generateInlineItems(BoxNode* node);

    // Measure a text run
    void measureText(InlineItem& item, float fontSize);

    // Find break opportunity (Unicode line breaking algorithm simplified)
    size_t findBreakPoint(const std::string& text, float maxWidth,
                            float fontSize, size_t startIndex);

    // Compute line height from inline items
    void computeLineMetrics(LineBox& line);

    // Apply text-align (left/center/right/justify)
    void alignLine(LineBox& line, const std::string& textAlign, bool isLastLine);

    // Apply vertical-align within a line
    void verticalAlignItems(LineBox& line);

    // White-space collapsing
    std::string collapseWhiteSpace(const std::string& text,
                                      const std::string& whiteSpaceProperty);
};

} // namespace Web
} // namespace NXRender
