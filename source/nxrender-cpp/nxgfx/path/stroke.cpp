// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "stroke.h"
#include <cmath>
#include <algorithm>

namespace NXRender {
namespace PathGen {

static const float PI = 3.14159265358979323846f;
static const float EPSILON = 1e-5f;

StrokeGenerator::StrokeGenerator(const StrokeOptions& options) : options_(options) {}

StrokeGenerator::~StrokeGenerator() {}

void StrokeGenerator::generateCap(const Point& p, const Point& normal, const Point& dir, bool isStart, std::vector<Point>& output) {
    float hw = options_.width * 0.5f;
    if (options_.cap == LineCap::Butt) {
        if (isStart) {
            output.push_back(Point(p.x + normal.x * hw, p.y + normal.y * hw));
            output.push_back(Point(p.x - normal.x * hw, p.y - normal.y * hw));
        } else {
            output.push_back(Point(p.x - normal.x * hw, p.y - normal.y * hw));
            output.push_back(Point(p.x + normal.x * hw, p.y + normal.y * hw));
        }
    } else if (options_.cap == LineCap::Square) {
        float ex = dir.x * hw;
        float ey = dir.y * hw;
        if (isStart) {
            output.push_back(Point(p.x - ex + normal.x * hw, p.y - ey + normal.y * hw));
            output.push_back(Point(p.x - ex - normal.x * hw, p.y - ey - normal.y * hw));
        } else {
            output.push_back(Point(p.x + ex - normal.x * hw, p.y + ey - normal.y * hw));
            output.push_back(Point(p.x + ex + normal.x * hw, p.y + ey + normal.y * hw));
        }
    } else if (options_.cap == LineCap::Round) {
        float startAngle = std::atan2(normal.y, normal.x);
        if (!isStart) startAngle += PI;

        int segments = std::max(4, static_cast<int>(std::ceil(options_.width * PI / 4.0f)));
        float theta = PI / segments;
        
        for (int i = 0; i <= segments; ++i) {
            float a = startAngle + (isStart ? i * theta : -i * theta);
            output.push_back(Point(p.x + std::cos(a) * hw, p.y + std::sin(a) * hw));
        }
    }
}

void StrokeGenerator::generateJoin(const Point& p, const Point& nA, const Point& prevDir, const Point& nB, const Point& currDir, std::vector<Point>& outputPos, std::vector<Point>& outputNeg) {
    float hw = options_.width * 0.5f;

    // Check collinearity
    float cross = prevDir.x * currDir.y - prevDir.y * currDir.x;
    if (std::abs(cross) < EPSILON) {
        outputPos.push_back(Point(p.x + nA.x * hw, p.y + nA.y * hw));
        outputNeg.push_back(Point(p.x - nA.x * hw, p.y - nA.y * hw));
        return;
    }

    // Determine outer/inner
    bool rightOuter = cross > 0;
    
    // Miter derivation: Inner corner is always a point
    float miterX = nA.x + nB.x;
    float miterY = nA.y + nB.y;
    float miterNormSq = miterX*miterX + miterY*miterY;
    
    float miterRatio = 2.0f / miterNormSq;
    float mtx = miterX * miterRatio;
    float mty = miterY * miterRatio;

    Point innerPoint(p.x - (rightOuter ? mtx : -mtx) * hw, p.y - (rightOuter ? mty : -mty) * hw);

    if (rightOuter) {
        outputNeg.push_back(innerPoint);
    } else {
        outputPos.push_back(innerPoint);
    }

    auto generateOuter = [&](std::vector<Point>& outList) {
        float limitSq = options_.miterLimit * options_.miterLimit;
        if (options_.join == LineJoin::Miter && miterRatio <= limitSq) {
            outList.push_back(Point(p.x + (rightOuter ? mtx : -mtx) * hw, p.y + (rightOuter ? mty : -mty) * hw));
        } else if (options_.join == LineJoin::Round) {
            float startAngle = std::atan2(nA.y, nA.x);
            float endAngle = std::atan2(nB.y, nB.x);
            if (rightOuter && endAngle < startAngle) endAngle += 2.0f * PI;
            if (!rightOuter && endAngle > startAngle) endAngle -= 2.0f * PI;

            float diff = endAngle - startAngle;
            int segments = std::max(2, static_cast<int>(std::ceil(std::abs(diff) * hw / 2.0f)));
            float theta = diff / segments;
            
            for (int i = 0; i <= segments; ++i) {
                float a = startAngle + i * theta;
                outList.push_back(Point(p.x + std::cos(a) * hw * (rightOuter?1:-1), p.y + std::sin(a) * hw * (rightOuter?1:-1)));
            }
        } else { // Bevel or fallback
            outList.push_back(Point(p.x + nA.x * hw * (rightOuter?1:-1), p.y + nA.y * hw * (rightOuter?1:-1)));
            outList.push_back(Point(p.x + nB.x * hw * (rightOuter?1:-1), p.y + nB.y * hw * (rightOuter?1:-1)));
        }
    };

    if (rightOuter) generateOuter(outputPos);
    else generateOuter(outputNeg);
}

std::vector<Point> StrokeGenerator::expandPath(const std::vector<Point>& inputLine, bool isClosed) {
    if (inputLine.size() < 2) return {};

    std::vector<Point> outputPos;
    std::vector<Point> outputNeg;
    float hw = options_.width * 0.5f;

    auto getNormal = [](const Point& p1, const Point& p2) {
        float dx = p2.x - p1.x;
        float dy = p2.y - p1.y;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len < EPSILON) return Point(0, 0);
        return Point(-dy / len, dx / len);
    };

    auto getDir = [](const Point& p1, const Point& p2) {
        float dx = p2.x - p1.x;
        float dy = p2.y - p1.y;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len < EPSILON) return Point(1, 0);
        return Point(dx / len, dy / len);
    };

    size_t count = inputLine.size();

    // Remove duplicates
    std::vector<Point> cleanLine;
    cleanLine.push_back(inputLine[0]);
    for(size_t i=1; i<inputLine.size(); ++i) {
        if(std::abs(inputLine[i].x - cleanLine.back().x) > EPSILON || 
           std::abs(inputLine[i].y - cleanLine.back().y) > EPSILON) {
            cleanLine.push_back(inputLine[i]);
        }
    }
    
    if (isClosed && cleanLine.size() > 2) {
        if(std::abs(cleanLine.front().x - cleanLine.back().x) < EPSILON &&
           std::abs(cleanLine.front().y - cleanLine.back().y) < EPSILON) {
            cleanLine.pop_back();
        }
    }
    
    count = cleanLine.size();
    if(count < 2) return {};

    std::vector<Point> normals(count);
    std::vector<Point> dirs(count);

    for (size_t i = 0; i < count; ++i) {
        size_t nextI = (i + 1) % count;
        if (!isClosed && i == count - 1) break;
        dirs[i] = getDir(cleanLine[i], cleanLine[nextI]);
        normals[i] = Point(-dirs[i].y, dirs[i].x);
    }
    if (!isClosed) {
        dirs[count - 1] = dirs[count - 2];
        normals[count - 1] = normals[count - 2];
    }

    if (!isClosed) {
        generateCap(cleanLine[0], normals[0], dirs[0], true, outputPos);
    }

    for (size_t i = 0; i < count; ++i) {
        if (!isClosed && (i == 0 || i == count - 1)) {
            // Edges handled by caps or straight extensions
            if (i == 0) continue;
        }

        size_t prevI = (i == 0) ? count - 1 : i - 1;
        
        if (isClosed || (i > 0 && i < count - 1)) {
            generateJoin(cleanLine[i], normals[prevI], dirs[prevI], normals[i], dirs[i], outputPos, outputNeg);
        }
    }

    if (!isClosed) {
        generateCap(cleanLine[count - 1], normals[count - 1], dirs[count - 1], false, outputNeg);
    } else {
        // Form a continuous loop
        generateJoin(cleanLine[0], normals[count-1], dirs[count-1], normals[0], dirs[0], outputPos, outputNeg);
    }

    std::vector<Point> finalContour = outputPos;
    for (auto it = outputNeg.rbegin(); it != outputNeg.rend(); ++it) {
        finalContour.push_back(*it);
    }

    return finalContour;
}

} // namespace PathGen
} // namespace NXRender
