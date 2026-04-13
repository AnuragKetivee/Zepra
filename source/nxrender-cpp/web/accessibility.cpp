// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "accessibility.h"
#include <algorithm>
#include <chrono>
#include <cctype>

namespace NXRender {
namespace Web {

// ==================================================================
// AccessibilityNode
// ==================================================================

int AccessibilityNode::nextId_ = 1;

AccessibilityNode::AccessibilityNode() : id_(nextId_++) {}
AccessibilityNode::~AccessibilityNode() = default;

void AccessibilityNode::appendChild(std::unique_ptr<AccessibilityNode> child) {
    if (!child) return;
    child->parent_ = this;
    children_.push_back(std::move(child));
}

bool AccessibilityNode::isActionable() const {
    switch (props_.role) {
        case AriaRole::Button:
        case AriaRole::Link:
        case AriaRole::Checkbox:
        case AriaRole::Radio:
        case AriaRole::TextBox:
        case AriaRole::Combobox:
        case AriaRole::Slider:
        case AriaRole::SpinButton:
        case AriaRole::Switch:
        case AriaRole::Tab:
        case AriaRole::MenuItem:
        case AriaRole::MenuItemCheckbox:
        case AriaRole::MenuItemRadio:
        case AriaRole::Option:
        case AriaRole::TreeItem:
            return true;
        default:
            return false;
    }
}

bool AccessibilityNode::isFocusable() const {
    if (props_.hidden || props_.disabled) return false;
    return isActionable();
}

// ==================================================================
// AccessibilityTreeBuilder
// ==================================================================

AccessibilityTreeBuilder::AccessibilityTreeBuilder() {}
AccessibilityTreeBuilder::~AccessibilityTreeBuilder() {}

AriaRole AccessibilityTreeBuilder::roleFromTag(const std::string& tag) const {
    if (tag == "a")           return AriaRole::Link;
    if (tag == "button")      return AriaRole::Button;
    if (tag == "input-checkbox" || tag == "checkbox")
                              return AriaRole::Checkbox;
    if (tag == "input-radio" || tag == "radio")
                              return AriaRole::Radio;
    if (tag == "input" || tag == "textarea")
                              return AriaRole::TextBox;
    if (tag == "select")      return AriaRole::Combobox;
    if (tag == "option")      return AriaRole::Option;
    if (tag == "img")         return AriaRole::Img;
    if (tag == "nav")         return AriaRole::Navigation;
    if (tag == "main")        return AriaRole::Main;
    if (tag == "header")      return AriaRole::Banner;
    if (tag == "footer")      return AriaRole::ContentInfo;
    if (tag == "aside")       return AriaRole::Complementary;
    if (tag == "section")     return AriaRole::Region;
    if (tag == "article")     return AriaRole::Article;
    if (tag == "form")        return AriaRole::Form;
    if (tag == "search")      return AriaRole::Search;
    if (tag == "h1" || tag == "h2" || tag == "h3" ||
        tag == "h4" || tag == "h5" || tag == "h6")
                              return AriaRole::Heading;
    if (tag == "ul" || tag == "ol")
                              return AriaRole::List;
    if (tag == "li")          return AriaRole::ListItem;
    if (tag == "table")       return AriaRole::Table;
    if (tag == "tr")          return AriaRole::Row;
    if (tag == "td")          return AriaRole::Cell;
    if (tag == "th")          return AriaRole::ColumnHeader;
    if (tag == "thead" || tag == "tbody" || tag == "tfoot")
                              return AriaRole::RowGroup;
    if (tag == "progress")    return AriaRole::ProgressBar;
    if (tag == "meter")       return AriaRole::ProgressBar;
    if (tag == "input-range") return AriaRole::Slider;
    if (tag == "dialog")      return AriaRole::Dialog;
    if (tag == "menu")        return AriaRole::Menu;
    if (tag == "menuitem")    return AriaRole::MenuItem;
    if (tag == "figure")      return AriaRole::Figure;
    if (tag == "p" || tag == "span" || tag == "div")
                              return AriaRole::Generic;
    return AriaRole::Generic;
}

bool AccessibilityTreeBuilder::isAccessible(const BoxNode* node) const {
    if (!node) return false;
    const auto& cv = node->computed();

    // display:none → not in ax tree
    if (cv.display == 0) return false;
    // visibility:hidden → still in tree but hidden
    // aria-hidden=true → special handling needed
    return true;
}

std::string AccessibilityTreeBuilder::extractTextContent(const BoxNode* node) const {
    if (!node) return "";
    if (node->isTextNode()) return node->text();

    std::string result;
    for (const auto& child : node->children()) {
        std::string childText = extractTextContent(child.get());
        if (!childText.empty()) {
            if (!result.empty()) result += ' ';
            result += childText;
        }
    }
    return result;
}

std::string AccessibilityTreeBuilder::computeName(const BoxNode* node) const {
    if (!node) return "";

    const std::string& tag = node->tag();

    // img: use alt text
    if (tag == "img") return node->text();

    // input with placeholder
    if (tag == "input") return node->text();

    // Heading, button, link — use text content
    if (tag == "button" || tag == "a" ||
        tag[0] == 'h' && tag.size() == 2 && tag[1] >= '1' && tag[1] <= '6') {
        return extractTextContent(node);
    }

    // label element — use text content
    if (tag == "label") return extractTextContent(node);

    // Fallback: text content for text nodes
    if (node->isTextNode()) return node->text();

    return "";
}

std::unique_ptr<AccessibilityNode> AccessibilityTreeBuilder::buildNode(const BoxNode* boxNode) {
    if (!isAccessible(boxNode)) return nullptr;

    auto axNode = std::make_unique<AccessibilityNode>();
    auto& props = axNode->properties();

    const std::string& tag = boxNode->tag();

    // Role
    props.role = roleFromTag(tag);

    // Name
    props.name = computeName(boxNode);

    // Heading level
    if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
        props.level = tag[1] - '0';
    }

    // Visibility
    props.hidden = (boxNode->computed().visibility != 0);

    // Link box node
    axNode->setBoxNode(boxNode);

    // Bounds from layout
    const auto& lb = boxNode->layoutBox();
    axNode->bounds() = {lb.x, lb.y, lb.width, lb.height};

    nodeCount_++;

    // Recurse children
    for (const auto& child : boxNode->children()) {
        auto childAx = buildNode(child.get());
        if (childAx) {
            axNode->appendChild(std::move(childAx));
        }
    }

    return axNode;
}

std::unique_ptr<AccessibilityNode> AccessibilityTreeBuilder::build(const BoxNode* root) {
    nodeCount_ = 0;
    if (!root) return nullptr;
    return buildNode(root);
}

void AccessibilityTreeBuilder::rebuildSubtree(AccessibilityNode* axNode,
                                                   const BoxNode* boxNode) {
    if (!axNode || !boxNode) return;

    // Clear existing children
    // (Would need mutable access — for now, rebuild is the primary path)
}

// ==================================================================
// AccessibilityManager
// ==================================================================

AccessibilityManager::AccessibilityManager() {}

AccessibilityManager& AccessibilityManager::instance() {
    static AccessibilityManager inst;
    return inst;
}

void AccessibilityManager::buildTree(const BoxNode* rootBox) {
    root_ = builder_.build(rootBox);

    boxToAx_.clear();
    idToAx_.clear();
    if (root_) {
        buildLookup(root_.get());
    }

    if (onTreeUpdate_) onTreeUpdate_();
}

void AccessibilityManager::buildLookup(AccessibilityNode* node) {
    if (!node) return;
    if (node->boxNode()) {
        boxToAx_[node->boxNode()] = node;
    }
    idToAx_[node->id()] = node;
    for (const auto& child : node->children()) {
        buildLookup(child.get());
    }
}

AccessibilityNode* AccessibilityManager::findByBox(const BoxNode* box) const {
    auto it = boxToAx_.find(box);
    return (it != boxToAx_.end()) ? it->second : nullptr;
}

AccessibilityNode* AccessibilityManager::findById(int id) const {
    auto it = idToAx_.find(id);
    return (it != idToAx_.end()) ? it->second : nullptr;
}

void AccessibilityManager::setFocusedNode(AccessibilityNode* node) {
    if (focused_ == node) return;
    focused_ = node;
    if (onFocusChange_) onFocusChange_(node);
}

void AccessibilityManager::announce(const std::string& text, const std::string& priority) {
    auto now = std::chrono::steady_clock::now();
    double ts = std::chrono::duration<double>(now.time_since_epoch()).count();
    Announcement ann{text, priority, ts};
    announcements_.push_back(ann);
    if (onAnnouncement_) onAnnouncement_(ann);
}

} // namespace Web
} // namespace NXRender
