// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "core/surface.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace NXRender {

static int bytesPerPixel(PixelFormat format) {
    switch (format) {
        case PixelFormat::RGBA8:
        case PixelFormat::BGRA8:
            return 4;
        case PixelFormat::RGB8:
            return 3;
        case PixelFormat::Alpha8:
            return 1;
    }
    return 4;
}

Surface::Surface(int width, int height, PixelFormat format)
    : width_(width)
    , height_(height)
    , format_(format) {
    stride_ = width * bytesPerPixel(format);
    size_t totalBytes = static_cast<size_t>(stride_) * static_cast<size_t>(height);
    pixels_ = std::make_unique<uint8_t[]>(totalBytes);
    clear();
}

Surface::~Surface() = default;

void Surface::clear(uint32_t color) {
    if (width_ <= 0 || height_ <= 0) return;

    if (format_ == PixelFormat::RGBA8 || format_ == PixelFormat::BGRA8) {
        uint32_t* p = reinterpret_cast<uint32_t*>(pixels_.get());
        int count = width_ * height_;
        if (color == 0) {
            std::memset(pixels_.get(), 0, static_cast<size_t>(stride_) * static_cast<size_t>(height_));
        } else {
            for (int i = 0; i < count; i++) {
                p[i] = color;
            }
        }
    } else if (format_ == PixelFormat::RGB8) {
        uint8_t r = static_cast<uint8_t>((color >> 24) & 0xFF);
        uint8_t g = static_cast<uint8_t>((color >> 16) & 0xFF);
        uint8_t b = static_cast<uint8_t>((color >> 8) & 0xFF);
        for (int y = 0; y < height_; y++) {
            uint8_t* row = pixels_.get() + y * stride_;
            for (int x = 0; x < width_; x++) {
                row[x * 3 + 0] = r;
                row[x * 3 + 1] = g;
                row[x * 3 + 2] = b;
            }
        }
    } else {
        uint8_t a = static_cast<uint8_t>(color & 0xFF);
        std::memset(pixels_.get(), a,
                    static_cast<size_t>(stride_) * static_cast<size_t>(height_));
    }
}

void Surface::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    if (width <= 0 || height <= 0) return;

    int oldWidth = width_;
    int oldHeight = height_;
    int oldStride = stride_;
    auto oldPixels = std::move(pixels_);

    width_ = width;
    height_ = height;
    stride_ = width * bytesPerPixel(format_);
    size_t totalBytes = static_cast<size_t>(stride_) * static_cast<size_t>(height);
    pixels_ = std::make_unique<uint8_t[]>(totalBytes);
    std::memset(pixels_.get(), 0, totalBytes);

    // Copy old pixel data that fits
    int copyWidth = std::min(oldWidth, width_) * bytesPerPixel(format_);
    int copyHeight = std::min(oldHeight, height_);
    for (int y = 0; y < copyHeight; y++) {
        std::memcpy(
            pixels_.get() + y * stride_,
            oldPixels.get() + y * oldStride,
            static_cast<size_t>(copyWidth)
        );
    }
}

void Surface::clearRect(const Rect& rect, uint32_t color) {
    int x0 = std::max(0, static_cast<int>(rect.x));
    int y0 = std::max(0, static_cast<int>(rect.y));
    int x1 = std::min(width_, static_cast<int>(rect.x + rect.width));
    int y1 = std::min(height_, static_cast<int>(rect.y + rect.height));
    if (x0 >= x1 || y0 >= y1) return;

    int bpp = bytesPerPixel(format_);

    for (int y = y0; y < y1; y++) {
        uint8_t* row = pixels_.get() + y * stride_ + x0 * bpp;
        int rowBytes = (x1 - x0) * bpp;

        if (bpp == 4) {
            uint32_t* p32 = reinterpret_cast<uint32_t*>(row);
            int count = x1 - x0;
            if (color == 0) {
                std::memset(row, 0, static_cast<size_t>(rowBytes));
            } else {
                for (int i = 0; i < count; i++) p32[i] = color;
            }
        } else if (bpp == 1) {
            std::memset(row, static_cast<int>(color & 0xFF), static_cast<size_t>(rowBytes));
        } else {
            // RGB8
            uint8_t r = static_cast<uint8_t>((color >> 24) & 0xFF);
            uint8_t g = static_cast<uint8_t>((color >> 16) & 0xFF);
            uint8_t b = static_cast<uint8_t>((color >> 8) & 0xFF);
            for (int x = 0; x < (x1 - x0); x++) {
                row[x * 3 + 0] = r;
                row[x * 3 + 1] = g;
                row[x * 3 + 2] = b;
            }
        }
    }
}

void Surface::blit(const Surface& src, int destX, int destY) {
    if (src.format() != format_) return; // No format conversion in blit
    blitRect(src, Rect(0, 0,
                       static_cast<float>(src.width()),
                       static_cast<float>(src.height())),
             destX, destY);
}

void Surface::blitRect(const Surface& src, const Rect& srcRect, int destX, int destY) {
    if (src.format() != format_) return;

    int bpp = bytesPerPixel(format_);

    int sx0 = std::max(0, static_cast<int>(srcRect.x));
    int sy0 = std::max(0, static_cast<int>(srcRect.y));
    int sx1 = std::min(src.width(), static_cast<int>(srcRect.x + srcRect.width));
    int sy1 = std::min(src.height(), static_cast<int>(srcRect.y + srcRect.height));
    if (sx0 >= sx1 || sy0 >= sy1) return;

    int copyW = sx1 - sx0;
    int copyH = sy1 - sy0;

    // Clip to dest bounds
    if (destX < 0) { sx0 -= destX; copyW += destX; destX = 0; }
    if (destY < 0) { sy0 -= destY; copyH += destY; destY = 0; }
    if (destX + copyW > width_) copyW = width_ - destX;
    if (destY + copyH > height_) copyH = height_ - destY;
    if (copyW <= 0 || copyH <= 0) return;

    for (int y = 0; y < copyH; y++) {
        const uint8_t* srcRow = src.pixels() + (sy0 + y) * src.stride() + sx0 * bpp;
        uint8_t* dstRow = pixels_.get() + (destY + y) * stride_ + destX * bpp;
        std::memcpy(dstRow, srcRow, static_cast<size_t>(copyW * bpp));
    }
}

void Surface::blitAlpha(const Surface& src, int destX, int destY) {
    if (format_ != PixelFormat::RGBA8 || src.format() != PixelFormat::RGBA8) return;

    int copyW = std::min(src.width(), width_ - destX);
    int copyH = std::min(src.height(), height_ - destY);
    if (copyW <= 0 || copyH <= 0) return;

    int srcOffX = 0, srcOffY = 0;
    if (destX < 0) { srcOffX = -destX; copyW += destX; destX = 0; }
    if (destY < 0) { srcOffY = -destY; copyH += destY; destY = 0; }
    if (copyW <= 0 || copyH <= 0) return;

    for (int y = 0; y < copyH; y++) {
        const uint8_t* srcRow = src.pixels() + (srcOffY + y) * src.stride() + srcOffX * 4;
        uint8_t* dstRow = pixels_.get() + (destY + y) * stride_ + destX * 4;

        for (int x = 0; x < copyW; x++) {
            uint8_t sr = srcRow[x * 4 + 0];
            uint8_t sg = srcRow[x * 4 + 1];
            uint8_t sb = srcRow[x * 4 + 2];
            uint8_t sa = srcRow[x * 4 + 3];

            if (sa == 0) continue;
            if (sa == 255) {
                dstRow[x * 4 + 0] = sr;
                dstRow[x * 4 + 1] = sg;
                dstRow[x * 4 + 2] = sb;
                dstRow[x * 4 + 3] = 255;
                continue;
            }

            // Alpha compositing: porter-duff src-over
            uint8_t dr = dstRow[x * 4 + 0];
            uint8_t dg = dstRow[x * 4 + 1];
            uint8_t db = dstRow[x * 4 + 2];
            uint8_t da = dstRow[x * 4 + 3];

            uint32_t alpha = sa;
            uint32_t invAlpha = 255 - sa;

            dstRow[x * 4 + 0] = static_cast<uint8_t>((sr * alpha + dr * invAlpha) / 255);
            dstRow[x * 4 + 1] = static_cast<uint8_t>((sg * alpha + dg * invAlpha) / 255);
            dstRow[x * 4 + 2] = static_cast<uint8_t>((sb * alpha + db * invAlpha) / 255);
            dstRow[x * 4 + 3] = static_cast<uint8_t>(sa + (da * invAlpha) / 255);
        }
    }
}

void Surface::premultiplyAlpha() {
    if (format_ != PixelFormat::RGBA8 && format_ != PixelFormat::BGRA8) return;

    uint8_t* p = pixels_.get();
    int count = width_ * height_;
    for (int i = 0; i < count; i++) {
        uint32_t a = p[i * 4 + 3];
        p[i * 4 + 0] = static_cast<uint8_t>((p[i * 4 + 0] * a) / 255);
        p[i * 4 + 1] = static_cast<uint8_t>((p[i * 4 + 1] * a) / 255);
        p[i * 4 + 2] = static_cast<uint8_t>((p[i * 4 + 2] * a) / 255);
    }
}

void Surface::unpremultiplyAlpha() {
    if (format_ != PixelFormat::RGBA8 && format_ != PixelFormat::BGRA8) return;

    uint8_t* p = pixels_.get();
    int count = width_ * height_;
    for (int i = 0; i < count; i++) {
        uint32_t a = p[i * 4 + 3];
        if (a == 0) continue;
        if (a == 255) continue;
        p[i * 4 + 0] = static_cast<uint8_t>(std::min(255u, (p[i * 4 + 0] * 255u) / a));
        p[i * 4 + 1] = static_cast<uint8_t>(std::min(255u, (p[i * 4 + 1] * 255u) / a));
        p[i * 4 + 2] = static_cast<uint8_t>(std::min(255u, (p[i * 4 + 2] * 255u) / a));
    }
}

void Surface::flipVertical() {
    if (height_ <= 1 || !pixels_) return;

    auto rowBuf = std::make_unique<uint8_t[]>(static_cast<size_t>(stride_));
    for (int y = 0; y < height_ / 2; y++) {
        uint8_t* top = pixels_.get() + y * stride_;
        uint8_t* bot = pixels_.get() + (height_ - 1 - y) * stride_;
        std::memcpy(rowBuf.get(), top, static_cast<size_t>(stride_));
        std::memcpy(top, bot, static_cast<size_t>(stride_));
        std::memcpy(bot, rowBuf.get(), static_cast<size_t>(stride_));
    }
}

void Surface::convertFormat(PixelFormat newFormat) {
    if (newFormat == format_) return;

    int newBpp = bytesPerPixel(newFormat);
    int newStride = width_ * newBpp;
    size_t newSize = static_cast<size_t>(newStride) * static_cast<size_t>(height_);
    auto newPixels = std::make_unique<uint8_t[]>(newSize);

    int oldBpp = bytesPerPixel(format_);

    for (int y = 0; y < height_; y++) {
        const uint8_t* srcRow = pixels_.get() + y * stride_;
        uint8_t* dstRow = newPixels.get() + y * newStride;

        for (int x = 0; x < width_; x++) {
            uint8_t r = 0, g = 0, b = 0, a = 255;

            // Read source pixel
            if (format_ == PixelFormat::RGBA8) {
                r = srcRow[x * oldBpp + 0];
                g = srcRow[x * oldBpp + 1];
                b = srcRow[x * oldBpp + 2];
                a = srcRow[x * oldBpp + 3];
            } else if (format_ == PixelFormat::BGRA8) {
                b = srcRow[x * oldBpp + 0];
                g = srcRow[x * oldBpp + 1];
                r = srcRow[x * oldBpp + 2];
                a = srcRow[x * oldBpp + 3];
            } else if (format_ == PixelFormat::RGB8) {
                r = srcRow[x * oldBpp + 0];
                g = srcRow[x * oldBpp + 1];
                b = srcRow[x * oldBpp + 2];
            } else if (format_ == PixelFormat::Alpha8) {
                a = srcRow[x];
            }

            // Write destination pixel
            if (newFormat == PixelFormat::RGBA8) {
                dstRow[x * newBpp + 0] = r;
                dstRow[x * newBpp + 1] = g;
                dstRow[x * newBpp + 2] = b;
                dstRow[x * newBpp + 3] = a;
            } else if (newFormat == PixelFormat::BGRA8) {
                dstRow[x * newBpp + 0] = b;
                dstRow[x * newBpp + 1] = g;
                dstRow[x * newBpp + 2] = r;
                dstRow[x * newBpp + 3] = a;
            } else if (newFormat == PixelFormat::RGB8) {
                dstRow[x * newBpp + 0] = r;
                dstRow[x * newBpp + 1] = g;
                dstRow[x * newBpp + 2] = b;
            } else if (newFormat == PixelFormat::Alpha8) {
                dstRow[x] = a;
            }
        }
    }

    pixels_ = std::move(newPixels);
    format_ = newFormat;
    stride_ = newStride;
}

uint32_t Surface::getPixel(int x, int y) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return 0;

    int bpp = bytesPerPixel(format_);
    const uint8_t* p = pixels_.get() + y * stride_ + x * bpp;

    if (bpp == 4) {
        return *reinterpret_cast<const uint32_t*>(p);
    } else if (bpp == 3) {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | 0xFF;
    } else {
        return p[0];
    }
}

void Surface::setPixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;

    int bpp = bytesPerPixel(format_);
    uint8_t* p = pixels_.get() + y * stride_ + x * bpp;

    if (bpp == 4) {
        *reinterpret_cast<uint32_t*>(p) = color;
    } else if (bpp == 3) {
        p[0] = static_cast<uint8_t>((color >> 24) & 0xFF);
        p[1] = static_cast<uint8_t>((color >> 16) & 0xFF);
        p[2] = static_cast<uint8_t>((color >> 8) & 0xFF);
    } else {
        p[0] = static_cast<uint8_t>(color & 0xFF);
    }
}

void Surface::fillRect(const Rect& rect, uint32_t color) {
    clearRect(rect, color);
}

size_t Surface::memoryUsage() const {
    return static_cast<size_t>(stride_) * static_cast<size_t>(height_);
}

bool Surface::isOpaque() const {
    if (format_ == PixelFormat::RGB8) return true;
    if (format_ != PixelFormat::RGBA8 && format_ != PixelFormat::BGRA8) return false;

    const uint8_t* p = pixels_.get();
    int count = width_ * height_;
    for (int i = 0; i < count; i++) {
        if (p[i * 4 + 3] != 255) return false;
    }
    return true;
}

std::unique_ptr<Surface> Surface::clone() const {
    auto copy = std::make_unique<Surface>(width_, height_, format_);
    std::memcpy(copy->pixels(), pixels_.get(),
                static_cast<size_t>(stride_) * static_cast<size_t>(height_));
    return copy;
}

std::unique_ptr<Surface> Surface::subSurface(const Rect& region) const {
    int sx = std::max(0, static_cast<int>(region.x));
    int sy = std::max(0, static_cast<int>(region.y));
    int sw = std::min(width_ - sx, static_cast<int>(region.width));
    int sh = std::min(height_ - sy, static_cast<int>(region.height));
    if (sw <= 0 || sh <= 0) return nullptr;

    auto sub = std::make_unique<Surface>(sw, sh, format_);
    int bpp = bytesPerPixel(format_);
    for (int y = 0; y < sh; y++) {
        const uint8_t* srcRow = pixels_.get() + (sy + y) * stride_ + sx * bpp;
        uint8_t* dstRow = sub->pixels() + y * sub->stride();
        std::memcpy(dstRow, srcRow, static_cast<size_t>(sw * bpp));
    }
    return sub;
}

std::unique_ptr<Surface> Surface::scaled(int newWidth, int newHeight) const {
    if (newWidth <= 0 || newHeight <= 0) return nullptr;

    auto result = std::make_unique<Surface>(newWidth, newHeight, format_);
    int bpp = bytesPerPixel(format_);

    // Bilinear interpolation
    float xRatio = static_cast<float>(width_ - 1) / static_cast<float>(newWidth);
    float yRatio = static_cast<float>(height_ - 1) / static_cast<float>(newHeight);

    for (int dy = 0; dy < newHeight; dy++) {
        float srcY = dy * yRatio;
        int sy0 = static_cast<int>(srcY);
        int sy1 = std::min(sy0 + 1, height_ - 1);
        float fy = srcY - sy0;

        for (int dx = 0; dx < newWidth; dx++) {
            float srcX = dx * xRatio;
            int sx0 = static_cast<int>(srcX);
            int sx1 = std::min(sx0 + 1, width_ - 1);
            float fx = srcX - sx0;

            uint8_t* dst = result->pixels() + dy * result->stride() + dx * bpp;

            for (int c = 0; c < bpp; c++) {
                float c00 = pixels_[sy0 * stride_ + sx0 * bpp + c];
                float c10 = pixels_[sy0 * stride_ + sx1 * bpp + c];
                float c01 = pixels_[sy1 * stride_ + sx0 * bpp + c];
                float c11 = pixels_[sy1 * stride_ + sx1 * bpp + c];

                float top = c00 + (c10 - c00) * fx;
                float bot = c01 + (c11 - c01) * fx;
                float val = top + (bot - top) * fy;

                dst[c] = static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f));
            }
        }
    }

    return result;
}

} // namespace NXRender
