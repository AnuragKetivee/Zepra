// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file surface.h
 * @brief Drawing surfaces for compositor
 */

#pragma once

#include "../nxgfx/primitives.h"
#include <cstdint>
#include <memory>

namespace NXRender {

/**
 * @brief Pixel format
 */
enum class PixelFormat {
    RGBA8,
    BGRA8,
    RGB8,
    Alpha8
};

/**
 * @brief Drawing surface (off-screen buffer)
 */
class Surface {
public:
    Surface(int width, int height, PixelFormat format = PixelFormat::RGBA8);
    ~Surface();
    
    // Properties
    int width() const { return width_; }
    int height() const { return height_; }
    Size size() const { return Size(static_cast<float>(width_), static_cast<float>(height_)); }
    PixelFormat format() const { return format_; }
    
    // Pixel access
    uint8_t* pixels() { return pixels_.get(); }
    const uint8_t* pixels() const { return pixels_.get(); }
    int stride() const { return stride_; }
    
    // Clear entire surface or a region
    void clear(uint32_t color = 0);
    void clearRect(const Rect& rect, uint32_t color = 0);
    
    // Resize (preserves existing pixels that fit)
    void resize(int width, int height);

    // Blit (copy pixels from src into this surface)
    void blit(const Surface& src, int destX, int destY);
    void blitRect(const Surface& src, const Rect& srcRect, int destX, int destY);

    // Alpha-composited blit (Porter-Duff src-over, RGBA8 only)
    void blitAlpha(const Surface& src, int destX, int destY);

    // Alpha premultiplication
    void premultiplyAlpha();
    void unpremultiplyAlpha();

    // Flip vertically (for GL readback)
    void flipVertical();

    // Format conversion (in-place)
    void convertFormat(PixelFormat newFormat);

    // Individual pixel access
    uint32_t getPixel(int x, int y) const;
    void setPixel(int x, int y, uint32_t color);
    void fillRect(const Rect& rect, uint32_t color);

    // Memory and state
    size_t memoryUsage() const;
    bool isOpaque() const;

    // Cloning and sub-regions
    std::unique_ptr<Surface> clone() const;
    std::unique_ptr<Surface> subSurface(const Rect& region) const;

    // Bilinear-scaled copy
    std::unique_ptr<Surface> scaled(int newWidth, int newHeight) const;
    
private:
    int width_;
    int height_;
    int stride_;
    PixelFormat format_;
    std::unique_ptr<uint8_t[]> pixels_;
};

} // namespace NXRender
