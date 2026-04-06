// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "path.h"
#include <vector>

namespace NXRender {
namespace PathGen {

struct BuilderOptions {
    float tolerance = 0.25f;       // Maximum error pixel distance for curve flattening
    float angleTolerance = 0.1f;   // Maximum angle for sharp subdivision
    bool enableDashing = false;    // Whether to generate a dashed overlay
    std::vector<float> dashArray;
    float dashOffset = 0.0f;
    float scale = 1.0f;            // Dynamic scale adjustment for transform preservation
};

class PathBuilder {
public:
    PathBuilder(const BuilderOptions& options = BuilderOptions());
    ~PathBuilder();

    // Core tessellation flattening routines converting complex curves into polygonal lines
    std::vector<Point> flatten(const Path& path);
    
    // Generates a secondary set of disjoint polygonal lines corresponding to dash boundaries
    std::vector<std::vector<Point>> flattenDashed(const Path& path);

private:
    BuilderOptions options_;

    // Recursive and iterative flattening subsystems
    void flattenLine(const Point& p0, const Point& p1, std::vector<Point>& output);
    void flattenQuad(const Point& p0, const Point& p1, const Point& p2, std::vector<Point>& output);
    void flattenCubic(const Point& p0, const Point& p1, const Point& p2, const Point& p3, std::vector<Point>& output);
    void flattenArc(const Point& p0, const ArcParams& arc, std::vector<Point>& output);

    // Dashing State Machine
    void processDashing(const std::vector<Point>& flatContour, std::vector<std::vector<Point>>& dashedOutput);
};

} // namespace PathGen
} // namespace NXRender
