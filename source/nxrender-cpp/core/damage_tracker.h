// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "nxgfx/primitives.h"
#include <vector>

namespace NXRender {

struct DamageStats {
    int rectCount = 0;
    float totalArea = 0;
    float boundsArea = 0;
    float efficiency = 0; // totalArea / boundsArea (1.0 = perfect)
    int frameNumber = 0;
};

class DamageTracker {
public:
    DamageTracker() = default;

    // Add damaged region
    void addDamage(const Rect& rect);
    void addDamagePoint(float x, float y, float radius = 1.0f);
    void unionDamage(const DamageTracker& other);

    // Subtract a clean (repainted) region
    void subtractClean(const Rect& cleanRect);

    void clear();

    const std::vector<Rect>& getDamageRects() const { return rects_; }
    Rect getBounds() const;
    bool hasDamage() const { return !rects_.empty(); }

    // Area analysis
    float totalDamageArea() const;
    float damageRatio(float totalArea) const;
    bool isFullDamage(float viewWidth, float viewHeight) const;

    // Optimization and alignment
    void optimize();
    std::vector<Rect> getTileAlignedDamage(int tileSize) const;
    void clipToViewport(float viewWidth, float viewHeight);

    // Frame lifecycle
    void beginFrame();
    DamageStats getStats() const;

    void setMaxRects(size_t max) { maxRects_ = max; }

private:
    bool mergeRects(const Rect& r1, const Rect& r2, Rect& out);
    void subtractRect(const Rect& src, const Rect& sub, std::vector<Rect>& out) const;

    std::vector<Rect> rects_;
    size_t maxRects_ = 64;
    int frameCount_ = 0;
};

} // namespace NXRender
