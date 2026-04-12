// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include "nxgfx/primitives.h"
#include <string>
#include <vector>
#include <cmath>
#include <array>
#include <cstdint>

namespace NXRender {

// ==================================================================
// 4x4 Homogeneous Transform Matrix (column-major, OpenGL convention)
//
// | m[0]  m[4]  m[8]   m[12] |
// | m[1]  m[5]  m[9]   m[13] |
// | m[2]  m[6]  m[10]  m[14] |
// | m[3]  m[7]  m[11]  m[15] |
// ==================================================================

class TransformMatrix {
public:
    TransformMatrix();

    // Identity
    static TransformMatrix identity();

    // 2D transforms (CSS transform functions)
    static TransformMatrix translate(float tx, float ty);
    static TransformMatrix translate3d(float tx, float ty, float tz);
    static TransformMatrix scale(float sx, float sy);
    static TransformMatrix scale3d(float sx, float sy, float sz);
    static TransformMatrix rotate(float angleRad);
    static TransformMatrix rotateX(float angleRad);
    static TransformMatrix rotateY(float angleRad);
    static TransformMatrix rotateZ(float angleRad);
    static TransformMatrix rotate3d(float x, float y, float z, float angleRad);
    static TransformMatrix skewX(float angleRad);
    static TransformMatrix skewY(float angleRad);
    static TransformMatrix skew(float ax, float ay);
    static TransformMatrix perspective(float d);

    // CSS matrix() and matrix3d()
    static TransformMatrix fromCSS2D(float a, float b, float c, float d, float tx, float ty);
    static TransformMatrix fromCSS3D(const float vals[16]);

    // Composition
    TransformMatrix operator*(const TransformMatrix& rhs) const;
    TransformMatrix& operator*=(const TransformMatrix& rhs);

    // Apply to point
    Point transformPoint(const Point& p) const;
    Point transformPoint3D(float x, float y, float z) const;

    // Apply to rect (returns bounding rect of transformed corners)
    Rect transformRect(const Rect& r) const;

    // Inverse (returns identity if singular)
    TransformMatrix inverse() const;
    float determinant() const;
    bool isInvertible() const { return std::abs(determinant()) > 1e-6f; }

    // Queries
    bool isIdentity() const;
    bool is2D() const;         // No Z components
    bool isTranslation() const;
    bool hasRotation() const;
    bool hasScale() const;

    // Decompose for interpolation (CSS transitions)
    struct Decomposed2D {
        float translateX = 0, translateY = 0;
        float scaleX = 1, scaleY = 1;
        float angle = 0;     // radians
        float skewXY = 0;
    };
    bool decompose2D(Decomposed2D& out) const;
    static TransformMatrix recompose2D(const Decomposed2D& d);

    // Interpolation (for CSS transitions/animations)
    static TransformMatrix interpolate(const TransformMatrix& from,
                                         const TransformMatrix& to, float t);

    // Access
    float& at(int row, int col) { return m_[col * 4 + row]; }
    float at(int row, int col) const { return m_[col * 4 + row]; }
    const float* data() const { return m_.data(); }

private:
    std::array<float, 16> m_;
};

// ==================================================================
// CSS transform-origin resolution
// ==================================================================

struct TransformOrigin {
    float x = 0.5f;    // 0..1 (percentage of element width)
    float y = 0.5f;
    float z = 0;

    static TransformOrigin parse(const std::string& str);
    Point resolve(float width, float height) const;
};

// ==================================================================
// CSS transform parser
//
// Parses: translate(), translateX(), translateY(), translate3d(),
//         scale(), scaleX(), scaleY(), scale3d(),
//         rotate(), rotateX(), rotateY(), rotateZ(), rotate3d(),
//         skew(), skewX(), skewY(),
//         perspective(), matrix(), matrix3d()
// ==================================================================

class TransformParser {
public:
    static TransformMatrix parse(const std::string& cssTransform);

private:
    struct Token {
        std::string function;
        std::vector<float> args;
    };

    static std::vector<Token> tokenize(const std::string& str);
    static TransformMatrix applyFunction(const Token& token);
    static float parseAngle(const std::string& value);
    static float parseLength(const std::string& value);
};

// ==================================================================
// CSS Filter — filter: blur() brightness() contrast() etc.
// ==================================================================

struct CSSFilter {
    enum class Type : uint8_t {
        None, Blur, Brightness, Contrast, DropShadow,
        Grayscale, HueRotate, Invert, Opacity, Saturate, Sepia,
        URL // SVG filter reference
    };

    Type type = Type::None;

    float amount = 0;     // Primary parameter

    // drop-shadow specific
    float shadowOffsetX = 0;
    float shadowOffsetY = 0;
    float shadowBlur = 0;
    uint32_t shadowColor = 0x000000FF;

    // URL specific
    std::string url;
};

class FilterParser {
public:
    static std::vector<CSSFilter> parse(const std::string& cssFilter);

private:
    static CSSFilter parseSingleFilter(const std::string& fn, const std::string& args);
};

// ==================================================================
// Filter Pipeline — applies filter chain to a pixel buffer
// ==================================================================

class FilterPipeline {
public:
    // Apply a filter chain to RGBA pixel data
    static void apply(uint8_t* pixels, int width, int height, int stride,
                       const std::vector<CSSFilter>& filters);

    // Individual filter operations
    static void blur(uint8_t* pixels, int width, int height, int stride, float radius);
    static void brightness(uint8_t* pixels, int width, int height, int stride, float factor);
    static void contrast(uint8_t* pixels, int width, int height, int stride, float factor);
    static void grayscale(uint8_t* pixels, int width, int height, int stride, float amount);
    static void hueRotate(uint8_t* pixels, int width, int height, int stride, float angleDeg);
    static void invert(uint8_t* pixels, int width, int height, int stride, float amount);
    static void opacity(uint8_t* pixels, int width, int height, int stride, float amount);
    static void saturate(uint8_t* pixels, int width, int height, int stride, float amount);
    static void sepia(uint8_t* pixels, int width, int height, int stride, float amount);
    static void dropShadow(uint8_t* pixels, int width, int height, int stride,
                             float offsetX, float offsetY, float blurRadius, uint32_t color);

private:
    // Gaussian kernel generation
    static std::vector<float> generateGaussianKernel(float radius);

    // Separable blur passes
    static void blurHorizontal(const uint8_t* src, uint8_t* dst, int width, int height,
                                 int stride, const std::vector<float>& kernel);
    static void blurVertical(const uint8_t* src, uint8_t* dst, int width, int height,
                               int stride, const std::vector<float>& kernel);

    // Color matrix application (for grayscale, sepia, hue-rotate, saturate)
    static void applyColorMatrix(uint8_t* pixels, int width, int height, int stride,
                                   const float matrix[20]);
};

} // namespace NXRender
