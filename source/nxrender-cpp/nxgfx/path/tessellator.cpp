// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "tessellator.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace NXRender {
namespace PathGen {

Tessellator::Tessellator() {}

Tessellator::~Tessellator() {}

void Tessellator::setOuterContour(const std::vector<Point>& contour) {
    outerContour_ = contour;
}

void Tessellator::addHole(const std::vector<Point>& holeContour) {
    if (!holeContour.empty()) {
        holes_.push_back(holeContour);
    }
}

float Tessellator::triangleArea(const Point& p, const Point& q, const Point& r) const {
    return (q.y - p.y) * (r.x - q.x) - (q.x - p.x) * (r.y - q.y);
}

bool Tessellator::pointInTriangle(const Point& pt, const Point& v1, const Point& v2, const Point& v3) const {
    auto sign = [](const Point& p1, const Point& p2, const Point& p3) {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    };

    float d1 = sign(pt, v1, v2);
    float d2 = sign(pt, v2, v3);
    float d3 = sign(pt, v3, v1);

    bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(has_neg && has_pos);
}

Tessellator::VertexNode* Tessellator::buildDoublyLinkedList(const std::vector<Point>& contour) {
    if (contour.size() < 3) return nullptr;

    VertexNode* head = nullptr;
    VertexNode* tail = nullptr;

    float area = 0.0f;
    for (size_t i = 0; i < contour.size(); ++i) {
        size_t j = (i + 1) % contour.size();
        area += (contour[i].x * contour[j].y) - (contour[j].x * contour[i].y);
    }

    // Standardize to CCW winding
    bool isCW = area < 0.0f;

    for (size_t i = 0; i < contour.size(); ++i) {
        size_t idx = isCW ? (contour.size() - 1 - i) : i;
        VertexNode* node = new VertexNode{contour[idx], static_cast<int>(idx), nullptr, nullptr, false, false};
        
        if (!head) {
            head = node;
            tail = node;
            node->prev = node;
            node->next = node;
        } else {
            node->prev = tail;
            node->next = head;
            tail->next = node;
            head->prev = node;
            tail = node;
        }
    }

    return head;
}

void Tessellator::destroyList(VertexNode* head) {
    if (!head) return;
    VertexNode* curr = head;
    do {
        VertexNode* next = curr->next;
        delete curr;
        curr = next;
    } while (curr != head);
}

void Tessellator::splitPolygon(VertexNode* a, VertexNode* b) {
    VertexNode* a2 = new VertexNode{a->p, a->index, a->prev, b, false, false};
    VertexNode* b2 = new VertexNode{b->p, b->index, a, b->next, false, false};
    
    a->prev->next = a2;
    a->prev = b2;
    b->next->prev = b2;
    b->next = a2;
}

Tessellator::VertexNode* Tessellator::findHoleBridge(VertexNode* holeNode, VertexNode* outerNode) {
    VertexNode* p = outerNode;
    float hx = holeNode->p.x;
    float hy = holeNode->p.y;
    float qx = -std::numeric_limits<float>::infinity();
    VertexNode* m = nullptr;

    // Find the maximal x projection on the target line
    do {
        if (hy <= p->p.y && hy >= p->next->p.y && p->next->p.y != p->p.y) {
            float x = p->p.x + (hy - p->p.y) * (p->next->p.x - p->p.x) / (p->next->p.y - p->p.y);
            if (x <= hx && x > qx) {
                qx = x;
                m = (p->p.x < p->next->p.x) ? p : p->next;
            }
        }
        p = p->next;
    } while (p != outerNode);

    if (!m) return p;

    // Check visibility against potential intersecting edges using sector confinement
    VertexNode* b = m;
    p = m->next;
    float bestTan = -std::numeric_limits<float>::infinity();

    while (p != m) {
        if (hx >= p->p.x && p->p.x >= m->p.x &&
            pointInTriangle(p->p, Point(hx, hy), m->p, Point(qx, hy))) {
            float t = std::abs(hy - p->p.y) / (hx - p->p.x);
            if (t > bestTan) {
                b = p;
                bestTan = t;
            }
        }
        p = p->next;
    }

    return b;
}

Tessellator::VertexNode* Tessellator::eliminateHoles(VertexNode* outerHead) {
    if (holes_.empty()) return outerHead;

    std::vector<VertexNode*> holeNodes;
    for (const auto& hole : holes_) {
        VertexNode* hNode = buildDoublyLinkedList(hole);
        if (hNode) {
            // Find leftmost node of hole
            VertexNode* curr = hNode;
            VertexNode* leftmost = hNode;
            do {
                if (curr->p.x < leftmost->p.x) leftmost = curr;
                curr = curr->next;
            } while (curr != hNode);
            holeNodes.push_back(leftmost);
        }
    }

    // Sort holes by leftmost X coordinate
    std::sort(holeNodes.begin(), holeNodes.end(), [](VertexNode* a, VertexNode* b) {
        return a->p.x > b->p.x;
    });

    for (VertexNode* hLeft : holeNodes) {
        VertexNode* bridge = findHoleBridge(hLeft, outerHead);
        if (bridge) {
            splitPolygon(bridge, hLeft);
        }
    }

    return outerHead;
}

bool Tessellator::isEar(VertexNode* ear) {
    if (triangleArea(ear->prev->p, ear->p, ear->next->p) >= 0) return false;

    VertexNode* p = ear->next->next;
    while (p != ear->prev) {
        if (pointInTriangle(p->p, ear->prev->p, ear->p, ear->next->p) &&
            triangleArea(p->prev->p, p->p, p->next->p) >= 0) {
            return false;
        }
        p = p->next;
    }
    return true;
}

Tessellator::VertexNode* Tessellator::earcutLinked(VertexNode* ear, std::vector<Triangle>& triangles) {
    if (!ear) return nullptr;

    VertexNode* stop = ear;
    
    // Compute initial ear flags
    VertexNode* curr = ear;
    do {
        curr->isEar = isEar(curr);
        curr = curr->next;
    } while (curr != ear);

    while (ear->prev != ear->next) {
        if (ear->isEar) {
            triangles.push_back({ear->prev->p, ear->p, ear->next->p});

            VertexNode* prevNode = ear->prev;
            VertexNode* nextNode = ear->next;
            
            prevNode->next = nextNode;
            nextNode->prev = prevNode;

            delete ear;

            // Re-evaluate adjacent nodes
            prevNode->isEar = isEar(prevNode);
            nextNode->isEar = isEar(nextNode);

            ear = nextNode;
            stop = nextNode;
            continue;
        }

        ear = ear->next;
        if (ear == stop) break; // Infinite loop fail-safe
    }

    return ear;
}

std::vector<Triangle> Tessellator::triangulate() {
    std::vector<Triangle> triangles;
    if (outerContour_.size() < 3) return triangles;

    VertexNode* head = buildDoublyLinkedList(outerContour_);
    if (!head) return triangles;

    head = eliminateHoles(head);
    VertexNode* remaining = earcutLinked(head, triangles);
    
    destroyList(remaining);
    
    return triangles;
}

} // namespace PathGen
} // namespace NXRender
