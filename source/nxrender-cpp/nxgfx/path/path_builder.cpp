// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "path_builder.h"
#include <cmath>
#include <algorithm>

namespace NXRender {
namespace PathGen {

static const float PI = 3.14159265358979323846f;
static const float TWO_PI = 2.0f * PI;
static const float EPSILON = 1e-5f;

PathBuilder::PathBuilder(const BuilderOptions& options) : options_(options) {
    if (options_.tolerance < 0.01f) options_.tolerance = 0.01f;
}

PathBuilder::~PathBuilder() {}

std::vector<Point> PathBuilder::flatten(const Path& path) {
    std::vector<Point> result;
    if (path.isEmpty()) return result;

    const auto& verbs = path.verbs();
    const auto& points = path.points();
    const auto& arcs = path.arcs();

    size_t ptIdx = 0;
    size_t arcIdx = 0;
    Point currentPoint(0,0);
    Point startPoint(0,0);

    for (PathVerb v : verbs) {
        switch (v) {
            case PathVerb::MoveTo:
                currentPoint = points[ptIdx++];
                startPoint = currentPoint;
                if (result.empty() || result.back().x != currentPoint.x || result.back().y != currentPoint.y) {
                    result.push_back(currentPoint);
                }
                break;
            case PathVerb::LineTo: {
                Point p1 = points[ptIdx++];
                flattenLine(currentPoint, p1, result);
                currentPoint = p1;
                break;
            }
            case PathVerb::QuadTo: {
                Point p1 = points[ptIdx++];
                Point p2 = points[ptIdx++];
                flattenQuad(currentPoint, p1, p2, result);
                currentPoint = p2;
                break;
            }
            case PathVerb::CubicTo: {
                Point p1 = points[ptIdx++];
                Point p2 = points[ptIdx++];
                Point p3 = points[ptIdx++];
                flattenCubic(currentPoint, p1, p2, p3, result);
                currentPoint = p3;
                break;
            }
            case PathVerb::ArcTo: {
                ArcParams arc = arcs[arcIdx++];
                flattenArc(currentPoint, arc, result);
                currentPoint = arc.endPoint;
                break;
            }
            case PathVerb::Close:
                if (currentPoint.x != startPoint.x || currentPoint.y != startPoint.y) {
                    flattenLine(currentPoint, startPoint, result);
                    currentPoint = startPoint;
                }
                break;
        }
    }
    return result;
}

void PathBuilder::flattenLine(const Point& p0, const Point& p1, std::vector<Point>& output) {
    if (output.empty() || output.back().x != p0.x || output.back().y != p0.y) {
        output.push_back(p0);
    }
    output.push_back(p1);
}

void PathBuilder::flattenQuad(const Point& p0, const Point& p1, const Point& p2, std::vector<Point>& output) {
    // Advanced recursive subdivision based on maximum chordal deviation
    float dx = p0.x - 2.0f * p1.x + p2.x;
    float dy = p0.y - 2.0f * p1.y + p2.y;
    float distSq = dx*dx + dy*dy;

    float effectiveTol = options_.tolerance / options_.scale;

    if (distSq <= effectiveTol * effectiveTol) {
        output.push_back(p2);
        return;
    }

    Point p01((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    Point p12((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);
    Point p012((p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f);

    flattenQuad(p0, p01, p012, output);
    flattenQuad(p012, p12, p2, output);
}

void PathBuilder::flattenCubic(const Point& p0, const Point& p1, const Point& p2, const Point& p3, std::vector<Point>& output) {
    // Distance from collinear control hulls
    float dirX = p3.x - p0.x;
    float dirY = p3.y - p0.y;
    float lenSq = dirX*dirX + dirY*dirY;

    float err = 0.0f;
    if (lenSq > 1e-6f) {
        float invLen = 1.0f / std::sqrt(lenSq);
        float nx = -dirY * invLen;
        float ny = dirX * invLen;

        float d1 = std::abs((p1.x - p0.x) * nx + (p1.y - p0.y) * ny);
        float d2 = std::abs((p2.x - p0.x) * nx + (p2.y - p0.y) * ny);
        err = std::max(d1, d2);
    } else {
        err = std::sqrt(std::pow(p1.x - p0.x, 2) + std::pow(p1.y - p0.y, 2)) +
              std::sqrt(std::pow(p2.x - p0.x, 2) + std::pow(p2.y - p0.y, 2));
    }

    float effectiveTol = options_.tolerance / options_.scale;
    if (err <= effectiveTol) {
        output.push_back(p3);
        return;
    }

    // Midpoint isolation (De Casteljau)
    Point p01((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    Point p12((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);
    Point p23((p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f);

    Point p012((p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f);
    Point p123((p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f);

    Point p0123((p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f);

    flattenCubic(p0, p01, p012, p0123, output);
    flattenCubic(p0123, p123, p23, p3, output);
}

void PathBuilder::flattenArc(const Point& p0, const ArcParams& arc, std::vector<Point>& output) {
    if (arc.rx <= EPSILON || arc.ry <= EPSILON) {
        flattenLine(p0, arc.endPoint, output);
        return;
    }

    // Standard Endpoint to Center Parameterization (SVG 1.1 Specification)
    float cosR = std::cos(arc.xAxisRotation);
    float sinR = std::sin(arc.xAxisRotation);

    float dx2 = (p0.x - arc.endPoint.x) / 2.0f;
    float dy2 = (p0.y - arc.endPoint.y) / 2.0f;

    float x1p = cosR * dx2 + sinR * dy2;
    float y1p = -sinR * dx2 + cosR * dy2;

    float rx_sq = arc.rx * arc.rx;
    float ry_sq = arc.ry * arc.ry;
    float x1p_sq = x1p * x1p;
    float y1p_sq = y1p * y1p;

    // Correct out of range radii
    float radiiCheck = x1p_sq / rx_sq + y1p_sq / ry_sq;
    float rx = arc.rx;
    float ry = arc.ry;
    if (radiiCheck > 1.0f) {
        float sqrtR = std::sqrt(radiiCheck);
        rx *= sqrtR;
        ry *= sqrtR;
        rx_sq = rx * rx;
        ry_sq = ry * ry;
    }

    float sign = (arc.largeArcFlag == arc.sweepFlag) ? -1.0f : 1.0f;
    float sq = ((rx_sq * ry_sq) - (rx_sq * y1p_sq) - (ry_sq * x1p_sq)) /
               ((rx_sq * y1p_sq) + (ry_sq * x1p_sq));
    sq = (sq < 0.0f) ? 0.0f : sq;
    float coef = sign * std::sqrt(sq);

    float cxp = coef * ((rx * y1p) / ry);
    float cyp = coef * (-(ry * x1p) / rx);

    float cx = cosR * cxp - sinR * cyp + (p0.x + arc.endPoint.x) / 2.0f;
    float cy = sinR * cxp + cosR * cyp + (p0.y + arc.endPoint.y) / 2.0f;

    auto angle = [](float ux, float uy, float vx, float vy) -> float {
        float sign = (ux * vy - uy * vx < 0.0f) ? -1.0f : 1.0f;
        float dot = ux * vx + uy * vy;
        float len = std::sqrt(ux*ux + uy*uy) * std::sqrt(vx*vx + vy*vy);
        float val = dot / len;
        if (val < -1.0f) val = -1.0f;
        if (val > 1.0f) val = 1.0f;
        return sign * std::acos(val);
    };

    float startAngle = angle(1.0f, 0.0f, (x1p - cxp)/rx, (y1p - cyp)/ry);
    float angleExtent = angle((x1p - cxp)/rx, (y1p - cyp)/ry, (-x1p - cxp)/rx, (-y1p - cyp)/ry);

    if (!arc.sweepFlag && angleExtent > 0.0f) angleExtent -= TWO_PI;
    else if (arc.sweepFlag && angleExtent < 0.0f) angleExtent += TWO_PI;

    // Map segments
    int segments = std::max(4, static_cast<int>(std::ceil(std::abs(angleExtent) / options_.angleTolerance)));
    float theta = angleExtent / segments;

    for (int i = 1; i <= segments; ++i) {
        float a = startAngle + i * theta;
        float ax = cx + rx * std::cos(a) * cosR - ry * std::sin(a) * sinR;
        float ay = cy + rx * std::cos(a) * sinR + ry * std::sin(a) * cosR;
        if (i == segments) {
            output.push_back(arc.endPoint);
        } else {
            output.push_back(Point(ax, ay));
        }
    }
}

std::vector<std::vector<Point>> PathBuilder::flattenDashed(const Path& path) {
    std::vector<Point> flat = flatten(path);
    std::vector<std::vector<Point>> result;

    if (!options_.enableDashing || options_.dashArray.empty()) {
        result.push_back(flat);
        return result;
    }

    // Heavyweight dash state machine
    float dashTotal = 0.0f;
    for (float v : options_.dashArray) dashTotal += v;
    if (dashTotal <= EPSILON) return {flat};

    size_t dashIdx = 0;
    bool isDashOn = true;
    float currentDashLen = options_.dashArray[0];
    
    float offset = options_.dashOffset;
    while (offset > dashTotal) offset -= dashTotal;
    while (offset < 0) offset += dashTotal;

    // Advance offset
    while (offset >= currentDashLen) {
        offset -= currentDashLen;
        dashIdx = (dashIdx + 1) % options_.dashArray.size();
        isDashOn = !isDashOn;
        currentDashLen = options_.dashArray[dashIdx];
    }
    
    float distanceRemainingInState = currentDashLen - offset;
    
    std::vector<Point> currentSubPath;
    if (isDashOn && !flat.empty()) currentSubPath.push_back(flat[0]);

    for (size_t i = 1; i < flat.size(); ++i) {
        Point p0 = flat[i-1];
        Point p1 = flat[i];
        
        float dx = p1.x - p0.x;
        float dy = p1.y - p0.y;
        float len = std::sqrt(dx*dx + dy*dy);
        
        float segmentConsumed = 0.0f;

        while (len - segmentConsumed > EPSILON) {
            float moveDist = std::min(len - segmentConsumed, distanceRemainingInState);
            float t = (segmentConsumed + moveDist) / len;
            Point mid(p0.x + dx * t, p0.y + dy * t);

            if (isDashOn) {
                currentSubPath.push_back(mid);
            }
            
            segmentConsumed += moveDist;
            distanceRemainingInState -= moveDist;

            if (distanceRemainingInState <= EPSILON) {
                if (isDashOn) {
                    result.push_back(currentSubPath);
                    currentSubPath.clear();
                }
                dashIdx = (dashIdx + 1) % options_.dashArray.size();
                isDashOn = !isDashOn;
                currentDashLen = options_.dashArray[dashIdx];
                distanceRemainingInState = currentDashLen;
                if (isDashOn) currentSubPath.push_back(mid);
            }
        }
    }
    
    if (isDashOn && !currentSubPath.empty() && currentSubPath.size() > 1) {
        result.push_back(currentSubPath);
    }

    return result;
}

} // namespace PathGen
} // namespace NXRender
