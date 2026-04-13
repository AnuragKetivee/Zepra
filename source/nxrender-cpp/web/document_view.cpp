// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "document_view.h"
#include "web/animation.h"
#include "web/events.h"

// WebCore includes — guarded
#ifdef USE_WEBCORE
#include "browser/dom.hpp"
#include "css/css_engine.hpp"
#include "css/css_computed_style.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace NXRender {
namespace Web {

// ==================================================================
// Tags that produce no visual output
// ==================================================================

static const std::unordered_set<std::string> kInvisibleTags = {
    "script", "style", "head", "meta", "link", "title", "base",
    "noscript", "template", "slot"
};

static const std::unordered_set<std::string> kVoidElements = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr"
};

bool DOMBoxBuilder::isInvisibleTag(const std::string& tag) {
    return kInvisibleTags.count(tag) > 0;
}

bool DOMBoxBuilder::isVoidElement(const std::string& tag) {
    return kVoidElements.count(tag) > 0;
}

// ==================================================================
// StyleBridge — WebCore CSSComputedStyle → NXRender ComputedValues
// ==================================================================

#ifdef USE_WEBCORE

uint8_t StyleBridge::mapDisplay(int d) {
    switch (static_cast<Zepra::WebCore::DisplayValue>(d)) {
        case Zepra::WebCore::DisplayValue::None:        return 0;
        case Zepra::WebCore::DisplayValue::Block:       return 1;
        case Zepra::WebCore::DisplayValue::Inline:      return 2;
        case Zepra::WebCore::DisplayValue::InlineBlock:  return 3;
        case Zepra::WebCore::DisplayValue::Flex:        return 4;
        case Zepra::WebCore::DisplayValue::InlineFlex:   return 5;
        case Zepra::WebCore::DisplayValue::Grid:        return 6;
        case Zepra::WebCore::DisplayValue::InlineGrid:   return 7;
        case Zepra::WebCore::DisplayValue::Table:       return 8;
        case Zepra::WebCore::DisplayValue::TableRow:     return 9;
        case Zepra::WebCore::DisplayValue::TableCell:    return 10;
        case Zepra::WebCore::DisplayValue::ListItem:     return 11;
        case Zepra::WebCore::DisplayValue::Contents:     return 12;
        default: return 1;
    }
}

uint8_t StyleBridge::mapPosition(int p) {
    switch (static_cast<Zepra::WebCore::PositionValue>(p)) {
        case Zepra::WebCore::PositionValue::Static:   return 0;
        case Zepra::WebCore::PositionValue::Relative: return 1;
        case Zepra::WebCore::PositionValue::Absolute: return 2;
        case Zepra::WebCore::PositionValue::Fixed:    return 3;
        case Zepra::WebCore::PositionValue::Sticky:   return 4;
        default: return 0;
    }
}

uint8_t StyleBridge::mapOverflow(int o) {
    switch (static_cast<Zepra::WebCore::OverflowValue>(o)) {
        case Zepra::WebCore::OverflowValue::Visible: return 0;
        case Zepra::WebCore::OverflowValue::Hidden:  return 1;
        case Zepra::WebCore::OverflowValue::Scroll:  return 2;
        case Zepra::WebCore::OverflowValue::Auto:    return 3;
        case Zepra::WebCore::OverflowValue::Clip:    return 4;
        default: return 0;
    }
}

uint8_t StyleBridge::mapFlexDirection(int d) {
    switch (static_cast<Zepra::WebCore::FlexDirection>(d)) {
        case Zepra::WebCore::FlexDirection::Row:           return 0;
        case Zepra::WebCore::FlexDirection::RowReverse:    return 1;
        case Zepra::WebCore::FlexDirection::Column:        return 2;
        case Zepra::WebCore::FlexDirection::ColumnReverse: return 3;
        default: return 0;
    }
}

uint8_t StyleBridge::mapJustifyAlign(int ja) {
    switch (static_cast<Zepra::WebCore::JustifyAlign>(ja)) {
        case Zepra::WebCore::JustifyAlign::Start:
        case Zepra::WebCore::JustifyAlign::FlexStart:  return 0;
        case Zepra::WebCore::JustifyAlign::End:
        case Zepra::WebCore::JustifyAlign::FlexEnd:    return 1;
        case Zepra::WebCore::JustifyAlign::Center:     return 2;
        case Zepra::WebCore::JustifyAlign::SpaceBetween: return 3;
        case Zepra::WebCore::JustifyAlign::SpaceAround:  return 4;
        case Zepra::WebCore::JustifyAlign::SpaceEvenly:  return 5;
        case Zepra::WebCore::JustifyAlign::Stretch:    return 6;
        case Zepra::WebCore::JustifyAlign::Baseline:   return 7;
        default: return 0;
    }
}

float StyleBridge::resolveLength(float value, int unit,
                                    float fontSize, float rootFontSize,
                                    float viewportW, float viewportH,
                                    float containerSize) {
    using U = Zepra::WebCore::CSSLength::Unit;
    switch (static_cast<U>(unit)) {
        case U::Px:      return value;
        case U::Em:      return value * fontSize;
        case U::Rem:     return value * rootFontSize;
        case U::Percent: return (containerSize > 0) ? value * containerSize / 100.0f : 0;
        case U::Vw:      return value * viewportW / 100.0f;
        case U::Vh:      return value * viewportH / 100.0f;
        case U::Vmin:    return value * std::min(viewportW, viewportH) / 100.0f;
        case U::Vmax:    return value * std::max(viewportW, viewportH) / 100.0f;
        case U::Ch:      return value * fontSize * 0.5f;
        case U::Ex:      return value * fontSize * 0.5f;
        case U::Cm:      return value * 96.0f / 2.54f;
        case U::Mm:      return value * 96.0f / 25.4f;
        case U::In:      return value * 96.0f;
        case U::Pt:      return value * 96.0f / 72.0f;
        case U::Pc:      return value * 96.0f / 6.0f;
        case U::Auto:    return 0;
        default:         return value;
    }
}

uint32_t StyleBridge::packColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (static_cast<uint32_t>(r) << 24) |
           (static_cast<uint32_t>(g) << 16) |
           (static_cast<uint32_t>(b) << 8) |
           static_cast<uint32_t>(a);
}

ComputedValues StyleBridge::convert(const Zepra::WebCore::CSSComputedStyle* wc,
                                       float vpW, float vpH,
                                       float parentFS, float rootFS) {
    ComputedValues cv;
    if (!wc) return cv;

    float fs = wc->fontSize > 0 ? wc->fontSize : parentFS;

    // Display / Position
    cv.display = mapDisplay(static_cast<int>(wc->display));
    cv.position = mapPosition(static_cast<int>(wc->position));
    cv.visibility = (wc->visibility == Zepra::WebCore::Visibility::Hidden) ? 1 :
                      (wc->visibility == Zepra::WebCore::Visibility::Collapse) ? 2 : 0;
    cv.boxSizing = (wc->boxSizing == Zepra::WebCore::BoxSizing::BorderBox) ? 1 : 0;
    cv.opacity = std::clamp(wc->opacity, 0.0f, 1.0f);

    // Dimensions
    cv.widthAuto = wc->width.isAuto();
    if (!cv.widthAuto) cv.width = resolveLength(wc->width.value, static_cast<int>(wc->width.unit), fs, rootFS, vpW, vpH);
    cv.heightAuto = wc->height.isAuto();
    if (!cv.heightAuto) cv.height = resolveLength(wc->height.value, static_cast<int>(wc->height.unit), fs, rootFS, vpW, vpH);
    cv.minWidth = resolveLength(wc->minWidth.value, static_cast<int>(wc->minWidth.unit), fs, rootFS, vpW, vpH);
    cv.minHeight = resolveLength(wc->minHeight.value, static_cast<int>(wc->minHeight.unit), fs, rootFS, vpW, vpH);
    cv.maxWidth = wc->maxWidth.isAuto() ? 1e9f : resolveLength(wc->maxWidth.value, static_cast<int>(wc->maxWidth.unit), fs, rootFS, vpW, vpH);
    cv.maxHeight = wc->maxHeight.isAuto() ? 1e9f : resolveLength(wc->maxHeight.value, static_cast<int>(wc->maxHeight.unit), fs, rootFS, vpW, vpH);

    // Margin
    cv.marginTopAuto = wc->marginTop.isAuto();
    if (!cv.marginTopAuto) cv.marginTop = resolveLength(wc->marginTop.value, static_cast<int>(wc->marginTop.unit), fs, rootFS, vpW, vpH);
    cv.marginRightAuto = wc->marginRight.isAuto();
    if (!cv.marginRightAuto) cv.marginRight = resolveLength(wc->marginRight.value, static_cast<int>(wc->marginRight.unit), fs, rootFS, vpW, vpH);
    cv.marginBottomAuto = wc->marginBottom.isAuto();
    if (!cv.marginBottomAuto) cv.marginBottom = resolveLength(wc->marginBottom.value, static_cast<int>(wc->marginBottom.unit), fs, rootFS, vpW, vpH);
    cv.marginLeftAuto = wc->marginLeft.isAuto();
    if (!cv.marginLeftAuto) cv.marginLeft = resolveLength(wc->marginLeft.value, static_cast<int>(wc->marginLeft.unit), fs, rootFS, vpW, vpH);

    // Padding
    cv.paddingTop = resolveLength(wc->paddingTop.value, static_cast<int>(wc->paddingTop.unit), fs, rootFS, vpW, vpH);
    cv.paddingRight = resolveLength(wc->paddingRight.value, static_cast<int>(wc->paddingRight.unit), fs, rootFS, vpW, vpH);
    cv.paddingBottom = resolveLength(wc->paddingBottom.value, static_cast<int>(wc->paddingBottom.unit), fs, rootFS, vpW, vpH);
    cv.paddingLeft = resolveLength(wc->paddingLeft.value, static_cast<int>(wc->paddingLeft.unit), fs, rootFS, vpW, vpH);

    // Border widths
    cv.borderTopWidth = wc->borderTopWidth;
    cv.borderRightWidth = wc->borderRightWidth;
    cv.borderBottomWidth = wc->borderBottomWidth;
    cv.borderLeftWidth = wc->borderLeftWidth;

    // Border colors
    cv.borderTopColor = packColor(wc->borderTopColor.r, wc->borderTopColor.g, wc->borderTopColor.b, wc->borderTopColor.a);
    cv.borderRightColor = packColor(wc->borderRightColor.r, wc->borderRightColor.g, wc->borderRightColor.b, wc->borderRightColor.a);
    cv.borderBottomColor = packColor(wc->borderBottomColor.r, wc->borderBottomColor.g, wc->borderBottomColor.b, wc->borderBottomColor.a);
    cv.borderLeftColor = packColor(wc->borderLeftColor.r, wc->borderLeftColor.g, wc->borderLeftColor.b, wc->borderLeftColor.a);

    // Border radius
    cv.borderTopLeftRadius = wc->borderTopLeftRadius;
    cv.borderTopRightRadius = wc->borderTopRightRadius;
    cv.borderBottomRightRadius = wc->borderBottomRightRadius;
    cv.borderBottomLeftRadius = wc->borderBottomLeftRadius;

    // Overflow
    cv.overflowX = mapOverflow(static_cast<int>(wc->overflowX));
    cv.overflowY = mapOverflow(static_cast<int>(wc->overflowY));

    // Positioning
    cv.topAuto = wc->top.isAuto();
    if (!cv.topAuto) cv.top = resolveLength(wc->top.value, static_cast<int>(wc->top.unit), fs, rootFS, vpW, vpH);
    cv.rightAuto = wc->right.isAuto();
    if (!cv.rightAuto) cv.right = resolveLength(wc->right.value, static_cast<int>(wc->right.unit), fs, rootFS, vpW, vpH);
    cv.bottomAuto = wc->bottom.isAuto();
    if (!cv.bottomAuto) cv.bottom = resolveLength(wc->bottom.value, static_cast<int>(wc->bottom.unit), fs, rootFS, vpW, vpH);
    cv.leftAuto = wc->left.isAuto();
    if (!cv.leftAuto) cv.left = resolveLength(wc->left.value, static_cast<int>(wc->left.unit), fs, rootFS, vpW, vpH);
    cv.zIndex = wc->zIndex;
    cv.zIndexAuto = wc->zIndexAuto;

    // Flexbox
    cv.flexDirection = mapFlexDirection(static_cast<int>(wc->flexDirection));
    cv.flexWrap = wc->flexWrap;
    cv.justifyContent = mapJustifyAlign(static_cast<int>(wc->justifyContent));
    cv.alignItems = mapJustifyAlign(static_cast<int>(wc->alignItems));
    cv.alignContent = mapJustifyAlign(static_cast<int>(wc->alignContent));
    cv.alignSelf = mapJustifyAlign(static_cast<int>(wc->alignSelf));
    cv.flexGrow = wc->flexGrow;
    cv.flexShrink = wc->flexShrink;
    cv.flexBasisAuto = wc->flexBasis.isAuto();
    if (!cv.flexBasisAuto) cv.flexBasis = resolveLength(wc->flexBasis.value, static_cast<int>(wc->flexBasis.unit), fs, rootFS, vpW, vpH);
    cv.order = wc->order;

    // Grid
    cv.gridTemplateColumns = wc->gridTemplateColumns;
    cv.gridTemplateRows = wc->gridTemplateRows;
    cv.rowGap = resolveLength(wc->rowGap.value, static_cast<int>(wc->rowGap.unit), fs, rootFS, vpW, vpH);
    cv.columnGap = resolveLength(wc->columnGap.value, static_cast<int>(wc->columnGap.unit), fs, rootFS, vpW, vpH);

    // Typography
    cv.color = packColor(wc->color.r, wc->color.g, wc->color.b, wc->color.a);
    cv.fontFamily = wc->fontFamily;
    cv.fontSize = fs;
    cv.fontWeight = static_cast<uint16_t>(wc->fontWeight);
    cv.fontStyle = (wc->fontStyle == Zepra::WebCore::FontStyle::Italic) ? 1 :
                     (wc->fontStyle == Zepra::WebCore::FontStyle::Oblique) ? 2 : 0;
    cv.lineHeight = wc->lineHeight;
    cv.textAlign = static_cast<uint8_t>(wc->textAlign);
    cv.letterSpacing = wc->letterSpacing;
    cv.wordSpacing = wc->wordSpacing;
    cv.textDecoration = wc->textDecoration;
    cv.textTransform = wc->textTransform;
    cv.whiteSpace = wc->whiteSpace;

    // Background
    cv.backgroundColor = packColor(wc->backgroundColor.r, wc->backgroundColor.g,
                                      wc->backgroundColor.b, wc->backgroundColor.a);
    cv.backgroundImage = wc->backgroundImage;
    cv.backgroundPosition = wc->backgroundPosition;
    cv.backgroundSize = wc->backgroundSize;
    cv.backgroundRepeat = wc->backgroundRepeat;

    // Effects
    cv.boxShadow = wc->boxShadow;
    cv.transform = wc->transform;
    cv.filter = wc->filter;
    cv.cursor = wc->cursor;
    cv.pointerEvents = wc->pointerEvents;
    cv.willChange = wc->willChange;
    cv.isolation = wc->isolation;
    cv.objectFit = wc->objectFit;
    cv.aspectRatio = wc->aspectRatio;

    return cv;
}

#else
// Stub when USE_WEBCORE is not defined
uint8_t StyleBridge::mapDisplay(int) { return 1; }
uint8_t StyleBridge::mapPosition(int) { return 0; }
uint8_t StyleBridge::mapOverflow(int) { return 0; }
uint8_t StyleBridge::mapFlexDirection(int) { return 0; }
uint8_t StyleBridge::mapJustifyAlign(int) { return 0; }
float StyleBridge::resolveLength(float v, int, float, float, float, float, float) { return v; }
uint32_t StyleBridge::packColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (r << 24) | (g << 16) | (b << 8) | a;
}
ComputedValues StyleBridge::convert(const Zepra::WebCore::CSSComputedStyle*, float, float, float, float) {
    return ComputedValues();
}
#endif

// ==================================================================
// DOMBoxBuilder
// ==================================================================

DOMBoxBuilder::DOMBoxBuilder() {}
DOMBoxBuilder::~DOMBoxBuilder() {}

BoxType DOMBoxBuilder::boxTypeFromDisplay(uint8_t display) {
    switch (display) {
        case 0:  return BoxType::None;
        case 1:  return BoxType::Block;
        case 2:  return BoxType::Inline;
        case 3:  return BoxType::InlineBlock;
        case 4:  return BoxType::Flex;
        case 5:  return BoxType::InlineFlex;
        case 6:  return BoxType::Grid;
        case 7:  return BoxType::InlineGrid;
        case 8:  return BoxType::Table;
        case 9:  return BoxType::TableRow;
        case 10: return BoxType::TableCell;
        case 11: return BoxType::ListItem;
        case 12: return BoxType::Contents;
        default: return BoxType::Block;
    }
}

void DOMBoxBuilder::maybeRegisterScroller(BoxNode* box) {
    auto& cv = box->computed();
    if (cv.overflowX >= 1 || cv.overflowY >= 1) {
        ScrollManager::instance().registerScrollContainer(box);
    }
}

BoxNode* DOMBoxBuilder::findBoxForDom(void* domNode) {
    auto it = domToBox_.find(domNode);
    return (it != domToBox_.end()) ? it->second : nullptr;
}

std::unique_ptr<BoxNode> DOMBoxBuilder::build(
    Zepra::WebCore::DOMDocument* doc,
    Zepra::WebCore::CSSEngine* css,
    const Options& opts) {

    opts_ = opts;
    nodeCount_ = 0;
    textNodeCount_ = 0;
    domToBox_.clear();

#ifdef USE_WEBCORE
    if (!doc) return nullptr;

    // Create viewport root box
    auto root = std::make_unique<BoxNode>();
    root->setTag("viewport");
    root->setBoxType(BoxType::Block);

    ComputedValues rootCV;
    rootCV.display = 1;
    rootCV.width = opts.viewportWidth;
    rootCV.widthAuto = false;
    rootCV.heightAuto = true;
    rootCV.fontSize = opts.rootFontSize;
    rootCV.backgroundColor = 0xFFFFFFFF;
    root->setComputedValues(rootCV);

    nodeCount_++;

    Zepra::WebCore::DOMElement* bodyElement = doc->body();
    if (!bodyElement) bodyElement = doc->documentElement();

    if (bodyElement) {
        const auto* bodyStyle = css ? css->getComputedStyle(bodyElement) : nullptr;
        float bodyFS = bodyStyle ? bodyStyle->fontSize : opts.rootFontSize;

        for (size_t i = 0; i < bodyElement->childNodes().size(); i++) {
            auto child = buildNode(bodyElement->childNodes()[i].get(), css, bodyFS);
            if (child) {
                root->appendChild(std::move(child));
            }
        }
    }

    return root;
#else
    (void)doc; (void)css;
    auto root = std::make_unique<BoxNode>();
    root->setTag("viewport");
    root->setBoxType(BoxType::Block);
    ComputedValues cv;
    cv.display = 1;
    cv.width = opts.viewportWidth;
    cv.widthAuto = false;
    root->setComputedValues(cv);
    return root;
#endif
}

#ifdef USE_WEBCORE
std::unique_ptr<BoxNode> DOMBoxBuilder::buildNode(
    Zepra::WebCore::DOMNode* node,
    Zepra::WebCore::CSSEngine* css,
    float parentFontSize) {

    if (!node) return nullptr;

    if (auto* textNode = dynamic_cast<Zepra::WebCore::DOMText*>(node)) {
        return buildTextNode(textNode, parentFontSize);
    }

    auto* element = dynamic_cast<Zepra::WebCore::DOMElement*>(node);
    if (!element) return nullptr;

    return buildElementNode(element, css, parentFontSize);
}

std::unique_ptr<BoxNode> DOMBoxBuilder::buildTextNode(
    Zepra::WebCore::DOMText* textNode,
    float parentFontSize) {

    std::string text = textNode->data();

    std::string normalized;
    bool lastSpace = true;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastSpace) { normalized += ' '; lastSpace = true; }
        } else {
            normalized += c;
            lastSpace = false;
        }
    }

    if (normalized.empty() || normalized == " ") return nullptr;

    auto box = BoxTreeBuilder::createTextBox(normalized, ComputedValues());
    box->setDomNode(textNode);
    box->computed().fontSize = parentFontSize;

    domToBox_[textNode] = box.get();
    nodeCount_++;
    textNodeCount_++;

    return box;
}

std::unique_ptr<BoxNode> DOMBoxBuilder::buildElementNode(
    Zepra::WebCore::DOMElement* element,
    Zepra::WebCore::CSSEngine* css,
    float parentFontSize) {

    std::string tag = element->tagName();
    std::transform(tag.begin(), tag.end(), tag.begin(),
                     [](unsigned char c) { return std::tolower(c); });

    if (isInvisibleTag(tag)) return nullptr;

    const auto* wcStyle = css ? css->getComputedStyle(element) : nullptr;

    if (wcStyle && wcStyle->display == Zepra::WebCore::DisplayValue::None) {
        return nullptr;
    }

    ComputedValues cv = StyleBridge::convert(wcStyle, opts_.viewportWidth, opts_.viewportHeight,
                                                parentFontSize, opts_.rootFontSize);

    float childFS = cv.fontSize > 0 ? cv.fontSize : parentFontSize;
    BoxType bt = boxTypeFromDisplay(cv.display);

    auto box = std::make_unique<BoxNode>();
    box->setTag(tag);
    box->setBoxType(bt);
    box->setComputedValues(cv);
    box->setDomNode(element);

    domToBox_[element] = box.get();
    nodeCount_++;

    maybeRegisterScroller(box.get());

    if (isVoidElement(tag)) {
        if (tag == "br") {
            box->setText("\n");
        } else if (tag == "hr") {
            if (cv.heightAuto) {
                box->computed().height = 1;
                box->computed().heightAuto = false;
            }
            if (cv.backgroundColor == 0) {
                box->computed().backgroundColor = 0xC8C8C8FF;
            }
        } else if (tag == "img") {
            std::string wAttr = element->getAttribute("width");
            std::string hAttr = element->getAttribute("height");
            if (!wAttr.empty() && cv.widthAuto) {
                box->computed().width = std::stof(wAttr);
                box->computed().widthAuto = false;
            }
            if (!hAttr.empty() && cv.heightAuto) {
                box->computed().height = std::stof(hAttr);
                box->computed().heightAuto = false;
            }
            std::string alt = element->getAttribute("alt");
            if (!alt.empty()) box->setText(alt);
            else box->setText("[image]");
        }
        return box;
    }

    for (size_t i = 0; i < element->childNodes().size(); i++) {
        auto child = buildNode(element->childNodes()[i].get(), css, childFS);
        if (child) {
            box->appendChild(std::move(child));
        }
    }

    return box;
}
#endif

void DOMBoxBuilder::rebuildSubtree(BoxNode* existingBox,
                                       Zepra::WebCore::DOMNode* domNode,
                                       Zepra::WebCore::CSSEngine* css) {
#ifdef USE_WEBCORE
    if (!existingBox || !domNode) return;

    while (existingBox->childCount() > 0) {
        existingBox->removeChild(existingBox->firstChild());
    }

    auto* element = dynamic_cast<Zepra::WebCore::DOMElement*>(domNode);
    if (!element) return;

    float parentFS = existingBox->computed().fontSize;
    for (size_t i = 0; i < element->childNodes().size(); i++) {
        auto child = buildNode(element->childNodes()[i].get(), css, parentFS);
        if (child) {
            existingBox->appendChild(std::move(child));
        }
    }
#else
    (void)existingBox; (void)domNode; (void)css;
#endif
}

// ==================================================================
// DocumentView — top-level rendering lifecycle
// ==================================================================

DocumentView::DocumentView() {}
DocumentView::~DocumentView() { detach(); }

void DocumentView::attach(Zepra::WebCore::DOMDocument* doc,
                              Zepra::WebCore::CSSEngine* css) {
    doc_ = doc;
    css_ = css;
    attached_ = true;

    rebuild();
    extractTitle();
}

void DocumentView::detach() {
    root_.reset();
    doc_ = nullptr;
    css_ = nullptr;
    attached_ = false;
}

void DocumentView::setViewport(float width, float height) {
    if (viewportW_ == width && viewportH_ == height) return;
    viewportW_ = width;
    viewportH_ = height;
    orchestrator_.setViewport(width, height);
    invalidateLayout();
}

void DocumentView::rebuild() {
    if (!doc_) return;

    DOMBoxBuilder::Options opts;
    opts.viewportWidth = viewportW_;
    opts.viewportHeight = viewportH_;

    root_ = builder_.build(doc_, css_, opts);
    if (root_) {
        orchestrator_.setRootBox(root_.get());
        orchestrator_.setViewport(viewportW_, viewportH_);
        invalidateStyle();
    }
}

void DocumentView::invalidateStyle() {
    orchestrator_.scheduleStyleRecalc();
}

void DocumentView::invalidateLayout() {
    orchestrator_.scheduleLayout();
}

void DocumentView::invalidatePaint() {
    orchestrator_.schedulePaint();
}

void DocumentView::renderFrame(double /*timestamp*/) {
    if (!root_) return;
    orchestrator_.renderFrame();
}

// Input routing
void DocumentView::onMouseDown(float x, float y, int button,
                                    bool shift, bool ctrl, bool alt, bool meta) {
    if (!root_) return;
    input_.onMouseDown(root_.get(), x, y, button, shift, ctrl, alt, meta);
}

void DocumentView::onMouseUp(float x, float y, int button,
                                  bool shift, bool ctrl, bool alt, bool meta) {
    if (!root_) return;
    input_.onMouseUp(root_.get(), x, y, button, shift, ctrl, alt, meta);
}

void DocumentView::onMouseMove(float x, float y,
                                    bool shift, bool ctrl, bool alt, bool meta) {
    if (!root_) return;
    input_.onMouseMove(root_.get(), x, y, shift, ctrl, alt, meta);
}

void DocumentView::onDoubleClick(float x, float y) {
    if (!root_) return;
    input_.onDoubleClick(root_.get(), x, y);
}

void DocumentView::onKeyDown(const std::string& key, const std::string& code,
                                   bool shift, bool ctrl, bool alt, bool meta) {
    if (!root_) return;
    input_.onKeyDown(root_.get(), key, code, shift, ctrl, alt, meta);
}

void DocumentView::onKeyUp(const std::string& key, const std::string& code,
                                 bool shift, bool ctrl, bool alt, bool meta) {
    if (!root_) return;
    input_.onKeyUp(root_.get(), key, code, shift, ctrl, alt, meta);
}

void DocumentView::onWheel(float x, float y, float deltaX, float deltaY) {
    if (!root_) return;
    input_.onWheel(root_.get(), x, y, deltaX, deltaY);
}

void DocumentView::onResize(float width, float height) {
    setViewport(width, height);
    rebuild();
}

void DocumentView::scrollTo(float x, float y, bool smooth) {
    if (!root_) return;
    ScrollManager::instance().scrollTo(root_.get(), x, y, smooth);
}

float DocumentView::scrollX() const {
    if (!root_) return 0;
    auto* state = ScrollManager::instance().getScrollState(const_cast<BoxNode*>(root_.get()));
    return state ? state->scrollLeft : 0;
}

float DocumentView::scrollY() const {
    if (!root_) return 0;
    auto* state = ScrollManager::instance().getScrollState(const_cast<BoxNode*>(root_.get()));
    return state ? state->scrollTop : 0;
}

HitTestResult DocumentView::hitTestAt(float x, float y) {
    HitTester tester;
    return tester.hitTest(root_.get(), x, y);
}

std::string DocumentView::getSelectedText() const {
    return Selection::instance().getSelectedText();
}

void DocumentView::selectAll() {
    if (root_) Selection::instance().selectAll(root_.get());
}

const FrameOrchestrator::FrameStats& DocumentView::lastFrameStats() const {
    return orchestrator_.lastFrameStats();
}

void DocumentView::extractTitle() {
#ifdef USE_WEBCORE
    if (!doc_ || !titleCallback_) return;
    std::string title = doc_->title();
    if (!title.empty()) {
        titleCallback_(title);
    }
#endif
}

} // namespace Web
} // namespace NXRender
