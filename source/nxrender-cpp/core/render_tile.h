// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "nxgfx/primitives.h"
#include <vector>
#include <cstdint>

namespace NXRender {
class GpuContext;

enum class TilePriority {
    Low,
    Normal,
    High,
    Critical
};

class RenderTile {
public:
    RenderTile(int x, int y, int width, int height);
    ~RenderTile();

    RenderTile(const RenderTile&) = delete;
    RenderTile& operator=(const RenderTile&) = delete;

    const Rect& bounds() const { return bounds_; }
    bool isReady() const { return isReady_; }
    TilePriority priority() const { return priority_; }
    int renderCount() const { return renderCount_; }
    
    // Binds this tile's FBO as the active render target
    void beginRecord(GpuContext* ctx);
    
    // Unbinds FBO, marks tile as ready
    void endRecord(GpuContext* ctx);

    // Draws the tile texture to the given screen rect
    void draw(GpuContext* ctx, const Rect& targetRect) const;
    
    void invalidate() { isReady_ = false; }
    void setPriority(TilePriority priority);

    // GPU resource tracking
    size_t memoryUsage() const;
    uint32_t textureId() const { return texture_; }

private:
    bool ensureFBO(int width, int height);
    void destroy();

    Rect bounds_;
    bool isReady_ = false;
    TilePriority priority_ = TilePriority::Normal;
    int renderCount_ = 0;

    uint32_t fbo_ = 0;
    uint32_t texture_ = 0;
    int fboWidth_ = 0;
    int fboHeight_ = 0;

    int savedViewport_[4] = {0, 0, 0, 0};
};

} // namespace NXRender
