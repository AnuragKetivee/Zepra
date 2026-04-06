// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "nxgfx/primitives.h"
#include <vector>
#include <string>
#include <cstdint>

namespace NXRender {
namespace PathGen {

enum class PathVerb : uint8_t {
    MoveTo,
    LineTo,
    QuadTo,
    CubicTo,
    ArcTo,    // Elliptical SVG-style arc
    Close
};

// Represents an elliptical arc following SVG A/a command specification
struct ArcParams {
    float rx, ry;
    float xAxisRotation; // In radians
    bool largeArcFlag;
    bool sweepFlag;
    Point endPoint;
};

class Path {
public:
    Path();
    ~Path();

    // Clears all contours and resets bounds
    void clear();

    // Standard commands
    void moveTo(float x, float y);
    void moveTo(const Point& p) { moveTo(p.x, p.y); }
    
    void lineTo(float x, float y);
    void lineTo(const Point& p) { lineTo(p.x, p.y); }
    
    void quadTo(float cx, float cy, float x, float y);
    void quadTo(const Point& cp, const Point& p) { quadTo(cp.x, cp.y, p.x, p.y); }
    
    void cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y);
    void cubicTo(const Point& cp1, const Point& cp2, const Point& p) { cubicTo(cp1.x, cp1.y, cp2.x, cp2.y, p.x, p.y); }

    // Adds an elliptical arc to the path
    void arcTo(float rx, float ry, float xAxisRotation, bool largeArcFlag, bool sweepFlag, float x, float y);

    void close();

    // SVG Parsing: Replaces the current path with the parsed data
    bool parseSvg(const std::string& d);

    // Bounding Box computation
    // computeBoundsExact() derives analytic maxima/minima rather than point enclosure
    void computeBoundsExact();
    const Rect& bounds() const { return bounds_; }

    // Introspection
    bool isEmpty() const { return verbs_.empty(); }
    const std::vector<PathVerb>& verbs() const { return verbs_; }
    const std::vector<Point>& points() const { return points_; }
    const std::vector<ArcParams>& arcs() const { return arcs_; }

private:
    std::vector<PathVerb> verbs_;
    std::vector<Point> points_;     // Stores points for Line, Quad, Cubic
    std::vector<ArcParams> arcs_;   // Stores parameters exclusively for ArcTo
    
    Rect bounds_;
    bool boundsDirty_ = true;

    // SVG Parsing state machine helpers
    static bool skipWhitespaceAndComma(const char*& ptr);
    static bool parseNumber(const char*& ptr, float& value);
    static bool parseFlag(const char*& ptr, bool& flag);

    // Analytic Bounding Box accumulators
    void updateBoundsPoint(const Point& p);
    void updateBoundsQuad(const Point& p0, const Point& p1, const Point& p2);
    void updateBoundsCubic(const Point& p0, const Point& p1, const Point& p2, const Point& p3);
    void updateBoundsArc(const Point& p0, const ArcParams& arc);
};

} // namespace PathGen
} // namespace NXRender
