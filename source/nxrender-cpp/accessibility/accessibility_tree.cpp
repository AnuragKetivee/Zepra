// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "accessibility/accessibility_tree.h"
#include "widgets/widget.h"
#include <sstream>
#include <algorithm>

namespace NXRender {

AccessibilityTree& AccessibilityTree::instance() {
    static AccessibilityTree tree;
    return tree;
}

void AccessibilityTree::build(Widget* root) {
    root_ = std::make_unique<AccessibleNode>(root);
    widgetMap_.clear();
    focusedNode_ = nullptr;

    if (!root) return;

    root_->setRole(AccessibleRole::Application);
    root_->setName(root->id() == 0 ? "Application" : std::to_string(root->id()));
    widgetMap_[root] = root_.get();

    const auto& kids = root->children();
    for (const auto& child : kids) {
        buildNode(child.get(), root_.get());
    }
}

void AccessibilityTree::buildNode(Widget* widget, AccessibleNode* parent) {
    if (!widget || !widget->isVisible()) return;

    auto node = std::make_unique<AccessibleNode>(widget);
    auto* nodePtr = node.get();

    node->setRole(inferRole(widget));
    node->setName(inferName(widget));

    // State
    auto& state = node->state();
    state.disabled = !widget->isEnabled();
    state.focused = widget->isFocused();
    state.hidden = !widget->isVisible();

    // Default actions based on role
    AccessibleRole role = node->role();
    if (role == AccessibleRole::Button) {
        node->addAction("press", "Activate the button", [widget]() {
            Event e;
            e.type = EventType::MouseDown;
            widget->handleEvent(e);
        });
    } else if (role == AccessibleRole::Checkbox) {
        node->addAction("toggle", "Toggle the checkbox", [widget]() {
            Event e;
            e.type = EventType::MouseDown;
            widget->handleEvent(e);
        });
    } else if (role == AccessibleRole::Link) {
        node->addAction("follow", "Follow the link", [widget]() {
            Event e;
            e.type = EventType::MouseDown;
            widget->handleEvent(e);
        });
    }

    widgetMap_[widget] = nodePtr;

    // Recurse
    const auto& kids = widget->children();
    for (const auto& child : kids) {
        buildNode(child.get(), nodePtr);
    }

    parent->addChild(std::move(node));
}

AccessibleRole AccessibilityTree::inferRole(Widget* widget) const {
    if (!widget) return AccessibleRole::None;

    // Use the widget's id as a heuristic for role inference
    std::string wid = std::to_string(widget->id());

    if (wid.find("button") != std::string::npos || wid.find("btn") != std::string::npos)
        return AccessibleRole::Button;
    if (wid.find("label") != std::string::npos)
        return AccessibleRole::Label;
    if (wid.find("checkbox") != std::string::npos || wid.find("check") != std::string::npos)
        return AccessibleRole::Checkbox;
    if (wid.find("radio") != std::string::npos)
        return AccessibleRole::RadioButton;
    if (wid.find("slider") != std::string::npos)
        return AccessibleRole::Slider;
    if (wid.find("text") != std::string::npos || wid.find("input") != std::string::npos)
        return AccessibleRole::TextBox;
    if (wid.find("list") != std::string::npos)
        return AccessibleRole::List;
    if (wid.find("tab") != std::string::npos)
        return AccessibleRole::Tab;
    if (wid.find("menu") != std::string::npos)
        return AccessibleRole::Menu;
    if (wid.find("dialog") != std::string::npos)
        return AccessibleRole::Dialog;
    if (wid.find("tooltip") != std::string::npos)
        return AccessibleRole::Tooltip;
    if (wid.find("progress") != std::string::npos)
        return AccessibleRole::ProgressBar;
    if (wid.find("scroll") != std::string::npos)
        return AccessibleRole::Region;
    if (wid.find("grid") != std::string::npos)
        return AccessibleRole::Grid;
    if (wid.find("dropdown") != std::string::npos)
        return AccessibleRole::List;
    if (wid.find("tree") != std::string::npos)
        return AccessibleRole::Tree;

    return AccessibleRole::Group;
}

std::string AccessibilityTree::inferName(Widget* widget) const {
    if (!widget) return "";
    if (widget->id() != 0) return std::to_string(widget->id());
    return "widget";
}

AccessibleNode* AccessibilityTree::nodeForWidget(Widget* widget) const {
    auto it = widgetMap_.find(widget);
    return (it != widgetMap_.end()) ? it->second : nullptr;
}

void AccessibilityTree::setFocus(AccessibleNode* node) {
    if (focusedNode_ == node) return;

    if (focusedNode_) {
        focusedNode_->state().focused = false;
        fireEvent(AccessibilityEvent::Blur, focusedNode_);
    }

    focusedNode_ = node;

    if (focusedNode_) {
        focusedNode_->state().focused = true;
        fireEvent(AccessibilityEvent::Focus, focusedNode_);
    }
}

bool AccessibilityTree::isFocusable(AccessibleNode* node) const {
    if (!node || !node->widget()) return false;
    if (node->state().disabled || node->state().hidden) return false;

    AccessibleRole role = node->role();
    return role == AccessibleRole::Button ||
           role == AccessibleRole::Checkbox ||
           role == AccessibleRole::RadioButton ||
           role == AccessibleRole::Slider ||
           role == AccessibleRole::TextBox ||
           role == AccessibleRole::Link ||
           role == AccessibleRole::MenuItem ||
           role == AccessibleRole::Tab ||
           role == AccessibleRole::TreeItem ||
           role == AccessibleRole::GridCell;
}

void AccessibilityTree::collectFocusable(AccessibleNode* node,
                                          std::vector<AccessibleNode*>& out) const {
    if (!node) return;
    if (isFocusable(node)) out.push_back(node);
    for (const auto& child : node->children()) {
        collectFocusable(child.get(), out);
    }
}

std::vector<AccessibleNode*> AccessibilityTree::focusableNodes() const {
    std::vector<AccessibleNode*> nodes;
    collectFocusable(root_.get(), nodes);
    return nodes;
}

AccessibleNode* AccessibilityTree::focusNext() {
    auto nodes = focusableNodes();
    if (nodes.empty()) return nullptr;

    if (!focusedNode_) {
        setFocus(nodes[0]);
        return focusedNode_;
    }

    for (size_t i = 0; i < nodes.size(); i++) {
        if (nodes[i] == focusedNode_) {
            size_t next = (i + 1) % nodes.size();
            setFocus(nodes[next]);
            return focusedNode_;
        }
    }

    setFocus(nodes[0]);
    return focusedNode_;
}

AccessibleNode* AccessibilityTree::focusPrevious() {
    auto nodes = focusableNodes();
    if (nodes.empty()) return nullptr;

    if (!focusedNode_) {
        setFocus(nodes.back());
        return focusedNode_;
    }

    for (size_t i = 0; i < nodes.size(); i++) {
        if (nodes[i] == focusedNode_) {
            size_t prev = (i > 0) ? i - 1 : nodes.size() - 1;
            setFocus(nodes[prev]);
            return focusedNode_;
        }
    }

    setFocus(nodes.back());
    return focusedNode_;
}

AccessibleNode* AccessibilityTree::focusFirst() {
    auto nodes = focusableNodes();
    if (nodes.empty()) return nullptr;
    setFocus(nodes[0]);
    return focusedNode_;
}

AccessibleNode* AccessibilityTree::focusLast() {
    auto nodes = focusableNodes();
    if (nodes.empty()) return nullptr;
    setFocus(nodes.back());
    return focusedNode_;
}

uint32_t AccessibilityTree::addListener(AccessibilityEventCallback callback) {
    uint32_t id = nextListenerId_++;
    listeners_.push_back({id, std::move(callback)});
    return id;
}

void AccessibilityTree::removeListener(uint32_t id) {
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
            [id](const Listener& l) { return l.id == id; }),
        listeners_.end()
    );
}

void AccessibilityTree::fireEvent(AccessibilityEvent event, AccessibleNode* node) {
    for (const auto& listener : listeners_) {
        listener.callback(event, node);
    }
}

void AccessibilityTree::notifyValueChanged(Widget* widget) {
    auto* node = nodeForWidget(widget);
    if (node) fireEvent(AccessibilityEvent::ValueChanged, node);
}

void AccessibilityTree::notifyStateChanged(Widget* widget) {
    auto* node = nodeForWidget(widget);
    if (node) fireEvent(AccessibilityEvent::StateChanged, node);
}

void AccessibilityTree::announce(const std::string& text, AccessibleNode::LivePriority priority) {
    if (!announceNode_) {
        announceNode_ = std::make_unique<AccessibleNode>();
        announceNode_->setRole(AccessibleRole::Alert);
    }

    announceNode_->setName(text);
    announceNode_->setLivePriority(priority);
    fireEvent(AccessibilityEvent::LiveRegionChanged, announceNode_.get());
}

std::vector<AccessibleNode*> AccessibilityTree::findByRole(AccessibleRole role) const {
    std::vector<AccessibleNode*> results;
    collectByRole(root_.get(), role, results);
    return results;
}

void AccessibilityTree::collectByRole(AccessibleNode* node, AccessibleRole role,
                                       std::vector<AccessibleNode*>& out) const {
    if (!node) return;
    if (node->role() == role) out.push_back(node);
    for (const auto& child : node->children()) {
        collectByRole(child.get(), role, out);
    }
}

AccessibleNode* AccessibilityTree::findByName(const std::string& name) const {
    return findByNameRecursive(root_.get(), name);
}

AccessibleNode* AccessibilityTree::findByNameRecursive(AccessibleNode* node,
                                                        const std::string& name) const {
    if (!node) return nullptr;
    if (node->name() == name) return node;
    for (const auto& child : node->children()) {
        auto* found = findByNameRecursive(child.get(), name);
        if (found) return found;
    }
    return nullptr;
}

AccessibleNode* AccessibilityTree::hitTest(float x, float y) const {
    return hitTestNode(root_.get(), x, y);
}

AccessibleNode* AccessibilityTree::hitTestNode(AccessibleNode* node, float x, float y) const {
    if (!node) return nullptr;

    for (auto it = node->children().rbegin(); it != node->children().rend(); ++it) {
        auto* hit = hitTestNode(it->get(), x, y);
        if (hit) return hit;
    }

    Rect bounds = node->bounds();
    if (!bounds.isEmpty() && bounds.contains(x, y)) {
        return node;
    }

    return nullptr;
}

std::string AccessibilityTree::describe() const {
    std::string output;
    output += "=== Accessibility Tree ===\n";
    describeNode(root_.get(), 0, output);
    return output;
}

void AccessibilityTree::describeNode(AccessibleNode* node, int depth, std::string& output) const {
    if (!node) return;

    std::string indent(static_cast<size_t>(depth) * 2, ' ');
    output += indent + "[" + node->roleString() + "] ";
    output += "\"" + node->name() + "\"";

    if (!node->value().empty()) output += " value=\"" + node->value() + "\"";
    if (node->state().disabled) output += " [disabled]";
    if (node->state().focused) output += " [focused]";
    if (node->state().checked) output += " [checked]";
    if (node->state().expanded) output += " [expanded]";
    if (!node->actions().empty()) {
        output += " actions=[";
        for (size_t i = 0; i < node->actions().size(); i++) {
            if (i > 0) output += ",";
            output += node->actions()[i].name;
        }
        output += "]";
    }
    output += "\n";

    for (const auto& child : node->children()) {
        describeNode(child.get(), depth + 1, output);
    }
}

} // namespace NXRender
