// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "nxgfx/primitives.h"
#include <vector>

namespace NXRender {
namespace PathGen {

enum class LineCap {
    Butt,
    Round,
    Square
};

enum class LineJoin {
    Miter,
    Round,
    Bevel
};

struct StrokeOptions {
    float width = 1.0f;
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    float miterLimit = 4.0f;
};

// Responsible for expanding a 1D polyline into a 2D boundary polygon (with self-overlap elimination).
class StrokeGenerator {
public:
    StrokeGenerator(const StrokeOptions& options = StrokeOptions());
    ~StrokeGenerator();

    // Ingests a flattened input path and outputs the expanded polygon border contour.
    std::vector<Point> expandPath(const std::vector<Point>& inputLine, bool isClosed);

private:
    StrokeOptions options_;

    struct VertexNormal {
        Point n;
        float len;
    };

    void generateCap(const Point& p, const Point& normal, const Point& dir, bool isStart, std::vector<Point>& output);
    void generateJoin(const Point& p, const Point& nA, const Point& prevDir, const Point& nB, const Point& currDir, std::vector<Point>& outputPos, std::vector<Point>& outputNeg);
};

} // namespace PathGen
} // namespace NXRender
