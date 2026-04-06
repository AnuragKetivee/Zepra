// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "nxgfx/primitives.h"
#include <vector>
#include <cstdint>

namespace NXRender {
namespace PathGen {

struct Triangle {
    Point a, b, c;
};

// Advanced Polygon Triangulator handling complex contours, holes, and non-convex geometries
class Tessellator {
public:
    Tessellator();
    ~Tessellator();

    // Sets the primary outer contour to be triangulated
    void setOuterContour(const std::vector<Point>& contour);
    
    // Injects a hole. Must be fully contained within the outer contour.
    void addHole(const std::vector<Point>& holeContour);

    // Executes algorithmic triangulation. Returns empty if invalid.
    std::vector<Triangle> triangulate();

private:
    struct VertexNode {
        Point p;
        int index;
        VertexNode* prev;
        VertexNode* next;
        bool isEar;
        bool isProcessed;
    };

    std::vector<Point> outerContour_;
    std::vector<std::vector<Point>> holes_;

    // Core algorithmic routines
    VertexNode* buildDoublyLinkedList(const std::vector<Point>& contour);
    void destroyList(VertexNode* head);
    
    // Hole Bridge resolution
    VertexNode* eliminateHoles(VertexNode* outerHead);
    VertexNode* findHoleBridge(VertexNode* hole, VertexNode* outer);
    void splitPolygon(VertexNode* a, VertexNode* b);

    // Classification
    bool isEar(VertexNode* node);
    bool pointInTriangle(const Point& pt, const Point& v1, const Point& v2, const Point& v3) const;
    float triangleArea(const Point& a, const Point& b, const Point& c) const;

    // Advanced spatial hashing could be mapped here for N log N complexity,
    // currently bounded optimized traversal
    VertexNode* earcutLinked(VertexNode* ear, std::vector<Triangle>& triangles);
};

} // namespace PathGen
} // namespace NXRender
