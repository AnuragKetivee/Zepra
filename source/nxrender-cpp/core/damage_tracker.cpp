// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "damage_tracker.h"
#include <algorithm>
#include <cmath>
#include <cassert>

namespace NXRender {

// ==================================================================
// DamageTracker — Rect-based damage tracking with spatial hashing
// ==================================================================

void DamageTracker::addDamage(const Rect& rect) {
    if (rect.width <= 0 || rect.height <= 0) return;

    // Try to merge with an existing rect
    for (size_t i = 0; i < rects_.size(); ++i) {
        Rect merged;
        if (mergeRects(rects_[i], rect, merged)) {
            float oldArea = rects_[i].width * rects_[i].height;
            float newArea = merged.width * merged.height;
            float srcArea = rect.width * rect.height;
            // Only merge if bounding box doesn't waste more than 50% area
            if (newArea <= (oldArea + srcArea) * 1.5f) {
                rects_[i] = merged;
                return;
            }
        }
    }

    rects_.push_back(rect);

    // If we accumulate too many rects, force a consolidation
    if (rects_.size() > maxRects_) {
        optimize();
    }
}

void DamageTracker::addDamagePoint(float x, float y, float radius) {
    addDamage(Rect(x - radius, y - radius, radius * 2, radius * 2));
}

void DamageTracker::unionDamage(const DamageTracker& other) {
    for (const auto& r : other.rects_) {
        addDamage(r);
    }
}

void DamageTracker::subtractClean(const Rect& cleanRect) {
    if (cleanRect.width <= 0 || cleanRect.height <= 0) return;

    std::vector<Rect> newRects;
    for (const auto& r : rects_) {
        subtractRect(r, cleanRect, newRects);
    }
    rects_ = std::move(newRects);
}

void DamageTracker::subtractRect(const Rect& src, const Rect& sub,
                                  std::vector<Rect>& out) const {
    // If no intersection, keep original
    if (!src.intersects(sub)) {
        out.push_back(src);
        return;
    }

    float srcRight = src.x + src.width;
    float srcBottom = src.y + src.height;
    float subRight = sub.x + sub.width;
    float subBottom = sub.y + sub.height;

    // Top strip
    if (sub.y > src.y) {
        float h = sub.y - src.y;
        if (h > 0) out.push_back(Rect(src.x, src.y, src.width, h));
    }

    // Bottom strip
    if (subBottom < srcBottom) {
        float h = srcBottom - subBottom;
        if (h > 0) out.push_back(Rect(src.x, subBottom, src.width, h));
    }

    // Left strip (between top and bottom strips)
    float midTop = std::max(src.y, sub.y);
    float midBot = std::min(srcBottom, subBottom);
    if (midTop < midBot) {
        if (sub.x > src.x) {
            float w = sub.x - src.x;
            if (w > 0) out.push_back(Rect(src.x, midTop, w, midBot - midTop));
        }
        if (subRight < srcRight) {
            float w = srcRight - subRight;
            if (w > 0) out.push_back(Rect(subRight, midTop, w, midBot - midTop));
        }
    }
}

void DamageTracker::clear() {
    rects_.clear();
    frameCount_ = 0;
}

Rect DamageTracker::getBounds() const {
    if (rects_.empty()) return Rect(0, 0, 0, 0);

    float minX = rects_[0].x;
    float minY = rects_[0].y;
    float maxX = rects_[0].x + rects_[0].width;
    float maxY = rects_[0].y + rects_[0].height;

    for (size_t i = 1; i < rects_.size(); ++i) {
        minX = std::min(minX, rects_[i].x);
        minY = std::min(minY, rects_[i].y);
        maxX = std::max(maxX, rects_[i].x + rects_[i].width);
        maxY = std::max(maxY, rects_[i].y + rects_[i].height);
    }

    return Rect(minX, minY, maxX - minX, maxY - minY);
}

float DamageTracker::totalDamageArea() const {
    float area = 0;
    for (const auto& r : rects_) {
        area += r.width * r.height;
    }
    return area;
}

float DamageTracker::damageRatio(float totalArea) const {
    if (totalArea <= 0) return 0;
    return totalDamageArea() / totalArea;
}

bool DamageTracker::isFullDamage(float viewWidth, float viewHeight) const {
    float viewArea = viewWidth * viewHeight;
    if (viewArea <= 0) return false;
    return damageRatio(viewArea) > 0.8f;
}

bool DamageTracker::mergeRects(const Rect& r1, const Rect& r2, Rect& out) {
    if (r1.intersects(r2)) {
        float minX = std::min(r1.x, r2.x);
        float minY = std::min(r1.y, r2.y);
        float maxX = std::max(r1.x + r1.width, r2.x + r2.width);
        float maxY = std::max(r1.y + r1.height, r2.y + r2.height);
        out = Rect(minX, minY, maxX - minX, maxY - minY);
        return true;
    }

    // Also merge if rects are very close (within 4px gap)
    const float gap = 4.0f;
    Rect expanded(r1.x - gap, r1.y - gap,
                  r1.width + gap * 2, r1.height + gap * 2);
    if (expanded.intersects(r2)) {
        float minX = std::min(r1.x, r2.x);
        float minY = std::min(r1.y, r2.y);
        float maxX = std::max(r1.x + r1.width, r2.x + r2.width);
        float maxY = std::max(r1.y + r1.height, r2.y + r2.height);
        out = Rect(minX, minY, maxX - minX, maxY - minY);
        return true;
    }

    return false;
}

void DamageTracker::optimize() {
    if (rects_.size() < 2) return;

    // Iterative merge pass
    bool merged;
    int passes = 0;
    do {
        merged = false;
        for (size_t i = 0; i < rects_.size(); ++i) {
            for (size_t j = i + 1; j < rects_.size(); ++j) {
                Rect out;
                if (mergeRects(rects_[i], rects_[j], out)) {
                    rects_[i] = out;
                    rects_.erase(rects_.begin() + static_cast<ptrdiff_t>(j));
                    merged = true;
                    break;
                }
            }
            if (merged) break;
        }
        passes++;
    } while (merged && passes < 32);

    // If still too many rects, collapse all to a single bounding box
    if (rects_.size() > maxRects_) {
        Rect bounds = getBounds();
        rects_.clear();
        rects_.push_back(bounds);
    }
}

std::vector<Rect> DamageTracker::getTileAlignedDamage(int tileSize) const {
    std::vector<Rect> tiles;
    if (tileSize <= 0) return tiles;

    float ts = static_cast<float>(tileSize);

    for (const auto& r : rects_) {
        // Snap to tile grid
        int startCol = static_cast<int>(std::floor(r.x / ts));
        int startRow = static_cast<int>(std::floor(r.y / ts));
        int endCol = static_cast<int>(std::ceil((r.x + r.width) / ts));
        int endRow = static_cast<int>(std::ceil((r.y + r.height) / ts));

        for (int row = startRow; row < endRow; row++) {
            for (int col = startCol; col < endCol; col++) {
                Rect tileRect(col * ts, row * ts, ts, ts);

                // Deduplicate: check if tile already added
                bool found = false;
                for (const auto& existing : tiles) {
                    if (existing.x == tileRect.x && existing.y == tileRect.y) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    tiles.push_back(tileRect);
                }
            }
        }
    }

    return tiles;
}

void DamageTracker::clipToViewport(float viewWidth, float viewHeight) {
    for (auto it = rects_.begin(); it != rects_.end(); ) {
        Rect& r = *it;

        // Clip to viewport
        float right = std::min(r.x + r.width, viewWidth);
        float bottom = std::min(r.y + r.height, viewHeight);
        r.x = std::max(r.x, 0.0f);
        r.y = std::max(r.y, 0.0f);
        r.width = right - r.x;
        r.height = bottom - r.y;

        if (r.width <= 0 || r.height <= 0) {
            it = rects_.erase(it);
        } else {
            ++it;
        }
    }
}

void DamageTracker::beginFrame() {
    frameCount_++;
}

DamageStats DamageTracker::getStats() const {
    DamageStats stats;
    stats.rectCount = static_cast<int>(rects_.size());
    stats.totalArea = totalDamageArea();
    stats.boundsArea = 0;
    if (!rects_.empty()) {
        Rect b = getBounds();
        stats.boundsArea = b.width * b.height;
    }
    stats.efficiency = (stats.boundsArea > 0)
        ? (stats.totalArea / stats.boundsArea) : 1.0f;
    stats.frameNumber = frameCount_;
    return stats;
}

} // namespace NXRender
