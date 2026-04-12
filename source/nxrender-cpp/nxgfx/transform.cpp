// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "transform.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace NXRender {

// ==================================================================
// TransformMatrix
// ==================================================================

TransformMatrix::TransformMatrix() {
    m_.fill(0);
    m_[0] = m_[5] = m_[10] = m_[15] = 1; // identity
}

TransformMatrix TransformMatrix::identity() {
    return TransformMatrix();
}

TransformMatrix TransformMatrix::translate(float tx, float ty) {
    TransformMatrix m;
    m.m_[12] = tx;
    m.m_[13] = ty;
    return m;
}

TransformMatrix TransformMatrix::translate3d(float tx, float ty, float tz) {
    TransformMatrix m;
    m.m_[12] = tx;
    m.m_[13] = ty;
    m.m_[14] = tz;
    return m;
}

TransformMatrix TransformMatrix::scale(float sx, float sy) {
    TransformMatrix m;
    m.m_[0] = sx;
    m.m_[5] = sy;
    return m;
}

TransformMatrix TransformMatrix::scale3d(float sx, float sy, float sz) {
    TransformMatrix m;
    m.m_[0] = sx;
    m.m_[5] = sy;
    m.m_[10] = sz;
    return m;
}

TransformMatrix TransformMatrix::rotate(float a) {
    TransformMatrix m;
    float c = std::cos(a), s = std::sin(a);
    m.m_[0] = c;  m.m_[4] = -s;
    m.m_[1] = s;  m.m_[5] = c;
    return m;
}

TransformMatrix TransformMatrix::rotateX(float a) {
    TransformMatrix m;
    float c = std::cos(a), s = std::sin(a);
    m.m_[5] = c;   m.m_[9] = -s;
    m.m_[6] = s;   m.m_[10] = c;
    return m;
}

TransformMatrix TransformMatrix::rotateY(float a) {
    TransformMatrix m;
    float c = std::cos(a), s = std::sin(a);
    m.m_[0] = c;   m.m_[8] = s;
    m.m_[2] = -s;  m.m_[10] = c;
    return m;
}

TransformMatrix TransformMatrix::rotateZ(float a) {
    return rotate(a);
}

TransformMatrix TransformMatrix::rotate3d(float x, float y, float z, float a) {
    // Normalize axis
    float len = std::sqrt(x * x + y * y + z * z);
    if (len < 1e-6f) return identity();
    x /= len; y /= len; z /= len;

    float c = std::cos(a), s = std::sin(a), t = 1 - c;
    TransformMatrix m;
    m.m_[0]  = t * x * x + c;
    m.m_[1]  = t * x * y + s * z;
    m.m_[2]  = t * x * z - s * y;
    m.m_[4]  = t * x * y - s * z;
    m.m_[5]  = t * y * y + c;
    m.m_[6]  = t * y * z + s * x;
    m.m_[8]  = t * x * z + s * y;
    m.m_[9]  = t * y * z - s * x;
    m.m_[10] = t * z * z + c;
    return m;
}

TransformMatrix TransformMatrix::skewX(float a) {
    TransformMatrix m;
    m.m_[4] = std::tan(a);
    return m;
}

TransformMatrix TransformMatrix::skewY(float a) {
    TransformMatrix m;
    m.m_[1] = std::tan(a);
    return m;
}

TransformMatrix TransformMatrix::skew(float ax, float ay) {
    TransformMatrix m;
    m.m_[4] = std::tan(ax);
    m.m_[1] = std::tan(ay);
    return m;
}

TransformMatrix TransformMatrix::perspective(float d) {
    TransformMatrix m;
    if (d > 0) m.m_[11] = -1.0f / d;
    return m;
}

TransformMatrix TransformMatrix::fromCSS2D(float a, float b, float c, float d,
                                              float tx, float ty) {
    TransformMatrix m;
    m.m_[0] = a;  m.m_[4] = c;  m.m_[12] = tx;
    m.m_[1] = b;  m.m_[5] = d;  m.m_[13] = ty;
    return m;
}

TransformMatrix TransformMatrix::fromCSS3D(const float vals[16]) {
    TransformMatrix m;
    std::memcpy(m.m_.data(), vals, 16 * sizeof(float));
    return m;
}

// ==================================================================
// Matrix multiplication (column-major)
// ==================================================================

TransformMatrix TransformMatrix::operator*(const TransformMatrix& rhs) const {
    TransformMatrix r;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += m_[k * 4 + row] * rhs.m_[col * 4 + k];
            }
            r.m_[col * 4 + row] = sum;
        }
    }
    return r;
}

TransformMatrix& TransformMatrix::operator*=(const TransformMatrix& rhs) {
    *this = *this * rhs;
    return *this;
}

// ==================================================================
// Point/Rect transformation
// ==================================================================

Point TransformMatrix::transformPoint(const Point& p) const {
    float w = m_[3] * p.x + m_[7] * p.y + m_[15];
    if (std::abs(w) < 1e-6f) w = 1;
    return Point(
        (m_[0] * p.x + m_[4] * p.y + m_[12]) / w,
        (m_[1] * p.x + m_[5] * p.y + m_[13]) / w
    );
}

Point TransformMatrix::transformPoint3D(float x, float y, float z) const {
    float w = m_[3] * x + m_[7] * y + m_[11] * z + m_[15];
    if (std::abs(w) < 1e-6f) w = 1;
    return Point(
        (m_[0] * x + m_[4] * y + m_[8]  * z + m_[12]) / w,
        (m_[1] * x + m_[5] * y + m_[9]  * z + m_[13]) / w
    );
}

Rect TransformMatrix::transformRect(const Rect& r) const {
    Point corners[4] = {
        transformPoint(Point(r.x, r.y)),
        transformPoint(Point(r.x + r.width, r.y)),
        transformPoint(Point(r.x + r.width, r.y + r.height)),
        transformPoint(Point(r.x, r.y + r.height))
    };

    float minX = corners[0].x, maxX = corners[0].x;
    float minY = corners[0].y, maxY = corners[0].y;
    for (int i = 1; i < 4; i++) {
        minX = std::min(minX, corners[i].x);
        maxX = std::max(maxX, corners[i].x);
        minY = std::min(minY, corners[i].y);
        maxY = std::max(maxY, corners[i].y);
    }
    return Rect(minX, minY, maxX - minX, maxY - minY);
}

// ==================================================================
// Determinant and inverse (cofactor expansion)
// ==================================================================

float TransformMatrix::determinant() const {
    // Expand along first row
    float a = m_[0], b = m_[4], c = m_[8],  d = m_[12];
    float e = m_[1], f = m_[5], g = m_[9],  h = m_[13];
    float i = m_[2], j = m_[6], k = m_[10], l = m_[14];
    float n = m_[3], o = m_[7], p = m_[11], q = m_[15];

    float kq_lp = k * q - l * p;
    float jq_lo = j * q - l * o;
    float jp_ko = j * p - k * o;
    float iq_ln = i * q - l * n;
    float ip_kn = i * p - k * n;
    float io_jn = i * o - j * n;

    return a * (f * kq_lp - g * jq_lo + h * jp_ko)
         - b * (e * kq_lp - g * iq_ln + h * ip_kn)
         + c * (e * jq_lo - f * iq_ln + h * io_jn)
         - d * (e * jp_ko - f * ip_kn + g * io_jn);
}

TransformMatrix TransformMatrix::inverse() const {
    float det = determinant();
    if (std::abs(det) < 1e-6f) return identity();

    float invDet = 1.0f / det;
    TransformMatrix inv;

    // Cofactor matrix (transposed) — each element is the minor's determinant × sign
    auto minor = [this](int r0, int r1, int r2, int c0, int c1, int c2) -> float {
        return m_[c0 * 4 + r0] * (m_[c1 * 4 + r1] * m_[c2 * 4 + r2] - m_[c2 * 4 + r1] * m_[c1 * 4 + r2])
             - m_[c1 * 4 + r0] * (m_[c0 * 4 + r1] * m_[c2 * 4 + r2] - m_[c2 * 4 + r1] * m_[c0 * 4 + r2])
             + m_[c2 * 4 + r0] * (m_[c0 * 4 + r1] * m_[c1 * 4 + r2] - m_[c1 * 4 + r1] * m_[c0 * 4 + r2]);
    };

    // Adjugate (transposed cofactor)
    inv.m_[0]  =  minor(1,2,3, 1,2,3) * invDet;
    inv.m_[1]  = -minor(1,2,3, 0,2,3) * invDet;
    inv.m_[2]  =  minor(1,2,3, 0,1,3) * invDet;
    inv.m_[3]  = -minor(1,2,3, 0,1,2) * invDet;
    inv.m_[4]  = -minor(0,2,3, 1,2,3) * invDet;
    inv.m_[5]  =  minor(0,2,3, 0,2,3) * invDet;
    inv.m_[6]  = -minor(0,2,3, 0,1,3) * invDet;
    inv.m_[7]  =  minor(0,2,3, 0,1,2) * invDet;
    inv.m_[8]  =  minor(0,1,3, 1,2,3) * invDet;
    inv.m_[9]  = -minor(0,1,3, 0,2,3) * invDet;
    inv.m_[10] =  minor(0,1,3, 0,1,3) * invDet;
    inv.m_[11] = -minor(0,1,3, 0,1,2) * invDet;
    inv.m_[12] = -minor(0,1,2, 1,2,3) * invDet;
    inv.m_[13] =  minor(0,1,2, 0,2,3) * invDet;
    inv.m_[14] = -minor(0,1,2, 0,1,3) * invDet;
    inv.m_[15] =  minor(0,1,2, 0,1,2) * invDet;

    return inv;
}

// ==================================================================
// Queries
// ==================================================================

bool TransformMatrix::isIdentity() const {
    return m_[0] == 1 && m_[5] == 1 && m_[10] == 1 && m_[15] == 1 &&
           m_[1] == 0 && m_[2] == 0 && m_[3] == 0 &&
           m_[4] == 0 && m_[6] == 0 && m_[7] == 0 &&
           m_[8] == 0 && m_[9] == 0 && m_[11] == 0 &&
           m_[12] == 0 && m_[13] == 0 && m_[14] == 0;
}

bool TransformMatrix::is2D() const {
    return m_[2] == 0 && m_[3] == 0 && m_[6] == 0 && m_[7] == 0 &&
           m_[8] == 0 && m_[9] == 0 && m_[10] == 1 && m_[11] == 0 &&
           m_[14] == 0 && m_[15] == 1;
}

bool TransformMatrix::isTranslation() const {
    return m_[0] == 1 && m_[1] == 0 && m_[4] == 0 && m_[5] == 1 &&
           m_[10] == 1 && m_[15] == 1;
}

bool TransformMatrix::hasRotation() const {
    return m_[1] != 0 || m_[4] != 0 || m_[2] != 0 || m_[6] != 0 || m_[8] != 0 || m_[9] != 0;
}

bool TransformMatrix::hasScale() const {
    return m_[0] != 1 || m_[5] != 1 || m_[10] != 1;
}

// ==================================================================
// 2D decomposition (for CSS transition interpolation)
// Per CSSOM spec: decompose → interpolate → recompose
// ==================================================================

bool TransformMatrix::decompose2D(Decomposed2D& out) const {
    if (!is2D()) return false;

    float a = m_[0], b = m_[1], c = m_[4], d = m_[5];
    float det = a * d - b * c;
    if (std::abs(det) < 1e-6f) return false;

    out.translateX = m_[12];
    out.translateY = m_[13];

    // Scale
    out.scaleX = std::sqrt(a * a + b * b);
    out.scaleY = std::sqrt(c * c + d * d);

    // Adjust for negative determinant
    if (det < 0) {
        out.scaleX = -out.scaleX;
    }

    // Rotation
    out.angle = std::atan2(b, a);

    // Skew
    float ac_bd = a * c + b * d;
    out.skewXY = ac_bd / (out.scaleX * out.scaleY);

    return true;
}

TransformMatrix TransformMatrix::recompose2D(const Decomposed2D& d) {
    TransformMatrix m = translate(d.translateX, d.translateY);
    m *= rotate(d.angle);
    if (d.skewXY != 0) m *= skewX(std::atan(d.skewXY));
    m *= scale(d.scaleX, d.scaleY);
    return m;
}

// ==================================================================
// Interpolation
// ==================================================================

TransformMatrix TransformMatrix::interpolate(const TransformMatrix& from,
                                                const TransformMatrix& to, float t) {
    // Try 2D decomposition first (most common case)
    Decomposed2D df, dt;
    if (from.decompose2D(df) && to.decompose2D(dt)) {
        Decomposed2D result;
        result.translateX = df.translateX + (dt.translateX - df.translateX) * t;
        result.translateY = df.translateY + (dt.translateY - df.translateY) * t;
        result.scaleX = df.scaleX + (dt.scaleX - df.scaleX) * t;
        result.scaleY = df.scaleY + (dt.scaleY - df.scaleY) * t;
        result.skewXY = df.skewXY + (dt.skewXY - df.skewXY) * t;

        // Angle interpolation (shortest path)
        float da = dt.angle - df.angle;
        if (da > M_PI) da -= 2 * M_PI;
        if (da < -M_PI) da += 2 * M_PI;
        result.angle = df.angle + da * t;

        return recompose2D(result);
    }

    // Fallback: element-wise lerp (not correct for 3D, but functional)
    TransformMatrix result;
    for (int i = 0; i < 16; i++) {
        result.m_[i] = from.m_[i] + (to.m_[i] - from.m_[i]) * t;
    }
    return result;
}

// ==================================================================
// TransformOrigin
// ==================================================================

TransformOrigin TransformOrigin::parse(const std::string& str) {
    TransformOrigin origin;
    if (str.empty()) return origin;

    std::istringstream ss(str);
    std::string xStr, yStr, zStr;

    if (ss >> xStr) {
        if (xStr == "left") origin.x = 0;
        else if (xStr == "center") origin.x = 0.5f;
        else if (xStr == "right") origin.x = 1;
        else if (xStr.back() == '%') origin.x = std::strtof(xStr.c_str(), nullptr) / 100.0f;
        else origin.x = std::strtof(xStr.c_str(), nullptr); // px (needs element size context)
    }

    if (ss >> yStr) {
        if (yStr == "top") origin.y = 0;
        else if (yStr == "center") origin.y = 0.5f;
        else if (yStr == "bottom") origin.y = 1;
        else if (yStr.back() == '%') origin.y = std::strtof(yStr.c_str(), nullptr) / 100.0f;
        else origin.y = std::strtof(yStr.c_str(), nullptr);
    }

    if (ss >> zStr) {
        origin.z = std::strtof(zStr.c_str(), nullptr);
    }

    return origin;
}

Point TransformOrigin::resolve(float width, float height) const {
    return Point(x * width, y * height);
}

// ==================================================================
// TransformParser
// ==================================================================

std::vector<TransformParser::Token> TransformParser::tokenize(const std::string& str) {
    std::vector<Token> tokens;
    size_t pos = 0;

    while (pos < str.size()) {
        // Skip whitespace
        while (pos < str.size() && std::isspace(str[pos])) pos++;
        if (pos >= str.size()) break;

        // Find function name
        size_t nameStart = pos;
        while (pos < str.size() && str[pos] != '(') pos++;
        if (pos >= str.size()) break;

        Token token;
        token.function = str.substr(nameStart, pos - nameStart);
        // Trim whitespace from function name
        while (!token.function.empty() && std::isspace(token.function.back()))
            token.function.pop_back();

        pos++; // skip '('

        // Parse arguments
        size_t argsStart = pos;
        int depth = 1;
        while (pos < str.size() && depth > 0) {
            if (str[pos] == '(') depth++;
            if (str[pos] == ')') depth--;
            pos++;
        }

        std::string argsStr = str.substr(argsStart, pos - argsStart - 1);
        std::istringstream argStream(argsStr);
        std::string arg;
        while (std::getline(argStream, arg, ',')) {
            while (!arg.empty() && std::isspace(arg.front())) arg.erase(arg.begin());
            while (!arg.empty() && std::isspace(arg.back())) arg.pop_back();
            if (!arg.empty()) {
                // Check if it's an angle
                if (arg.find("deg") != std::string::npos ||
                    arg.find("rad") != std::string::npos ||
                    arg.find("turn") != std::string::npos ||
                    arg.find("grad") != std::string::npos) {
                    token.args.push_back(parseAngle(arg));
                } else {
                    token.args.push_back(parseLength(arg));
                }
            }
        }

        tokens.push_back(token);
    }

    return tokens;
}

float TransformParser::parseAngle(const std::string& value) {
    float num = std::strtof(value.c_str(), nullptr);
    if (value.find("rad") != std::string::npos) return num;
    if (value.find("turn") != std::string::npos) return num * 2 * static_cast<float>(M_PI);
    if (value.find("grad") != std::string::npos) return num * static_cast<float>(M_PI) / 200.0f;
    // degrees (default)
    return num * static_cast<float>(M_PI) / 180.0f;
}

float TransformParser::parseLength(const std::string& value) {
    return std::strtof(value.c_str(), nullptr);
}

TransformMatrix TransformParser::applyFunction(const Token& token) {
    const auto& fn = token.function;
    const auto& a = token.args;

    if (fn == "translate" && a.size() >= 1)
        return TransformMatrix::translate(a[0], a.size() > 1 ? a[1] : 0);
    if (fn == "translateX" && a.size() >= 1) return TransformMatrix::translate(a[0], 0);
    if (fn == "translateY" && a.size() >= 1) return TransformMatrix::translate(0, a[0]);
    if (fn == "translate3d" && a.size() >= 3)
        return TransformMatrix::translate3d(a[0], a[1], a[2]);

    if (fn == "scale" && a.size() >= 1)
        return TransformMatrix::scale(a[0], a.size() > 1 ? a[1] : a[0]);
    if (fn == "scaleX" && a.size() >= 1) return TransformMatrix::scale(a[0], 1);
    if (fn == "scaleY" && a.size() >= 1) return TransformMatrix::scale(1, a[0]);
    if (fn == "scale3d" && a.size() >= 3)
        return TransformMatrix::scale3d(a[0], a[1], a[2]);

    if (fn == "rotate" && a.size() >= 1) return TransformMatrix::rotate(a[0]);
    if (fn == "rotateX" && a.size() >= 1) return TransformMatrix::rotateX(a[0]);
    if (fn == "rotateY" && a.size() >= 1) return TransformMatrix::rotateY(a[0]);
    if (fn == "rotateZ" && a.size() >= 1) return TransformMatrix::rotateZ(a[0]);
    if (fn == "rotate3d" && a.size() >= 4)
        return TransformMatrix::rotate3d(a[0], a[1], a[2], a[3]);

    if (fn == "skew" && a.size() >= 1)
        return TransformMatrix::skew(a[0], a.size() > 1 ? a[1] : 0);
    if (fn == "skewX" && a.size() >= 1) return TransformMatrix::skewX(a[0]);
    if (fn == "skewY" && a.size() >= 1) return TransformMatrix::skewY(a[0]);

    if (fn == "perspective" && a.size() >= 1) return TransformMatrix::perspective(a[0]);

    if (fn == "matrix" && a.size() >= 6)
        return TransformMatrix::fromCSS2D(a[0], a[1], a[2], a[3], a[4], a[5]);

    if (fn == "matrix3d" && a.size() >= 16) {
        float vals[16];
        for (int i = 0; i < 16; i++) vals[i] = a[i];
        return TransformMatrix::fromCSS3D(vals);
    }

    return TransformMatrix::identity();
}

TransformMatrix TransformParser::parse(const std::string& cssTransform) {
    if (cssTransform.empty() || cssTransform == "none") return TransformMatrix::identity();

    auto tokens = tokenize(cssTransform);
    TransformMatrix result = TransformMatrix::identity();
    for (const auto& token : tokens) {
        result *= applyFunction(token);
    }
    return result;
}

// ==================================================================
// FilterParser
// ==================================================================

std::vector<CSSFilter> FilterParser::parse(const std::string& cssFilter) {
    std::vector<CSSFilter> filters;
    if (cssFilter.empty() || cssFilter == "none") return filters;

    size_t pos = 0;
    while (pos < cssFilter.size()) {
        while (pos < cssFilter.size() && std::isspace(cssFilter[pos])) pos++;
        if (pos >= cssFilter.size()) break;

        size_t nameStart = pos;
        while (pos < cssFilter.size() && cssFilter[pos] != '(') pos++;
        if (pos >= cssFilter.size()) break;

        std::string fn = cssFilter.substr(nameStart, pos - nameStart);
        while (!fn.empty() && std::isspace(fn.back())) fn.pop_back();

        pos++; // skip '('
        size_t argsStart = pos;
        int depth = 1;
        while (pos < cssFilter.size() && depth > 0) {
            if (cssFilter[pos] == '(') depth++;
            if (cssFilter[pos] == ')') depth--;
            pos++;
        }
        std::string args = cssFilter.substr(argsStart, pos - argsStart - 1);

        filters.push_back(parseSingleFilter(fn, args));
    }

    return filters;
}

CSSFilter FilterParser::parseSingleFilter(const std::string& fn, const std::string& args) {
    CSSFilter f;

    if (fn == "blur") {
        f.type = CSSFilter::Type::Blur;
        f.amount = std::strtof(args.c_str(), nullptr);
    } else if (fn == "brightness") {
        f.type = CSSFilter::Type::Brightness;
        f.amount = std::strtof(args.c_str(), nullptr);
        if (args.back() == '%') f.amount /= 100.0f;
    } else if (fn == "contrast") {
        f.type = CSSFilter::Type::Contrast;
        f.amount = std::strtof(args.c_str(), nullptr);
        if (args.back() == '%') f.amount /= 100.0f;
    } else if (fn == "grayscale") {
        f.type = CSSFilter::Type::Grayscale;
        f.amount = std::strtof(args.c_str(), nullptr);
        if (args.back() == '%') f.amount /= 100.0f;
    } else if (fn == "hue-rotate") {
        f.type = CSSFilter::Type::HueRotate;
        f.amount = std::strtof(args.c_str(), nullptr); // degrees
    } else if (fn == "invert") {
        f.type = CSSFilter::Type::Invert;
        f.amount = std::strtof(args.c_str(), nullptr);
        if (args.back() == '%') f.amount /= 100.0f;
    } else if (fn == "opacity") {
        f.type = CSSFilter::Type::Opacity;
        f.amount = std::strtof(args.c_str(), nullptr);
        if (args.back() == '%') f.amount /= 100.0f;
    } else if (fn == "saturate") {
        f.type = CSSFilter::Type::Saturate;
        f.amount = std::strtof(args.c_str(), nullptr);
        if (args.back() == '%') f.amount /= 100.0f;
    } else if (fn == "sepia") {
        f.type = CSSFilter::Type::Sepia;
        f.amount = std::strtof(args.c_str(), nullptr);
        if (args.back() == '%') f.amount /= 100.0f;
    } else if (fn == "drop-shadow") {
        f.type = CSSFilter::Type::DropShadow;
        std::istringstream ss(args);
        std::string tok;
        if (ss >> tok) f.shadowOffsetX = std::strtof(tok.c_str(), nullptr);
        if (ss >> tok) f.shadowOffsetY = std::strtof(tok.c_str(), nullptr);
        if (ss >> tok) f.shadowBlur = std::strtof(tok.c_str(), nullptr);
    } else if (fn == "url") {
        f.type = CSSFilter::Type::URL;
        f.url = args;
    }

    return f;
}

// ==================================================================
// FilterPipeline — actual pixel operations
// ==================================================================

void FilterPipeline::apply(uint8_t* pixels, int width, int height, int stride,
                              const std::vector<CSSFilter>& filters) {
    for (const auto& f : filters) {
        switch (f.type) {
            case CSSFilter::Type::Blur:
                blur(pixels, width, height, stride, f.amount);
                break;
            case CSSFilter::Type::Brightness:
                brightness(pixels, width, height, stride, f.amount);
                break;
            case CSSFilter::Type::Contrast:
                contrast(pixels, width, height, stride, f.amount);
                break;
            case CSSFilter::Type::Grayscale:
                grayscale(pixels, width, height, stride, f.amount);
                break;
            case CSSFilter::Type::HueRotate:
                hueRotate(pixels, width, height, stride, f.amount);
                break;
            case CSSFilter::Type::Invert:
                invert(pixels, width, height, stride, f.amount);
                break;
            case CSSFilter::Type::Opacity:
                opacity(pixels, width, height, stride, f.amount);
                break;
            case CSSFilter::Type::Saturate:
                saturate(pixels, width, height, stride, f.amount);
                break;
            case CSSFilter::Type::Sepia:
                sepia(pixels, width, height, stride, f.amount);
                break;
            case CSSFilter::Type::DropShadow:
                dropShadow(pixels, width, height, stride,
                             f.shadowOffsetX, f.shadowOffsetY, f.shadowBlur, f.shadowColor);
                break;
            default:
                break;
        }
    }
}

// ==================================================================
// Gaussian blur (separable, two-pass)
// ==================================================================

std::vector<float> FilterPipeline::generateGaussianKernel(float radius) {
    int size = static_cast<int>(std::ceil(radius * 3)) * 2 + 1;
    if (size < 3) size = 3;
    std::vector<float> kernel(size);

    float sigma = radius;
    float sum = 0;
    int half = size / 2;

    for (int i = 0; i < size; i++) {
        float x = static_cast<float>(i - half);
        kernel[i] = std::exp(-(x * x) / (2 * sigma * sigma));
        sum += kernel[i];
    }

    // Normalize
    for (auto& k : kernel) k /= sum;
    return kernel;
}

void FilterPipeline::blurHorizontal(const uint8_t* src, uint8_t* dst,
                                       int width, int height, int stride,
                                       const std::vector<float>& kernel) {
    int half = static_cast<int>(kernel.size()) / 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float r = 0, g = 0, b = 0, a = 0;

            for (int k = 0; k < static_cast<int>(kernel.size()); k++) {
                int sx = std::clamp(x + k - half, 0, width - 1);
                const uint8_t* p = src + y * stride + sx * 4;
                float w = kernel[k];
                r += p[0] * w;
                g += p[1] * w;
                b += p[2] * w;
                a += p[3] * w;
            }

            uint8_t* d = dst + y * stride + x * 4;
            d[0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f));
            d[1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f));
            d[2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f));
            d[3] = static_cast<uint8_t>(std::clamp(a, 0.0f, 255.0f));
        }
    }
}

void FilterPipeline::blurVertical(const uint8_t* src, uint8_t* dst,
                                     int width, int height, int stride,
                                     const std::vector<float>& kernel) {
    int half = static_cast<int>(kernel.size()) / 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float r = 0, g = 0, b = 0, a = 0;

            for (int k = 0; k < static_cast<int>(kernel.size()); k++) {
                int sy = std::clamp(y + k - half, 0, height - 1);
                const uint8_t* p = src + sy * stride + x * 4;
                float w = kernel[k];
                r += p[0] * w;
                g += p[1] * w;
                b += p[2] * w;
                a += p[3] * w;
            }

            uint8_t* d = dst + y * stride + x * 4;
            d[0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f));
            d[1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f));
            d[2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f));
            d[3] = static_cast<uint8_t>(std::clamp(a, 0.0f, 255.0f));
        }
    }
}

void FilterPipeline::blur(uint8_t* pixels, int width, int height, int stride, float radius) {
    if (radius <= 0) return;

    auto kernel = generateGaussianKernel(radius);
    std::vector<uint8_t> temp(height * stride);

    // Horizontal pass
    blurHorizontal(pixels, temp.data(), width, height, stride, kernel);
    // Vertical pass
    blurVertical(temp.data(), pixels, width, height, stride, kernel);
}

// ==================================================================
// Point operations
// ==================================================================

void FilterPipeline::brightness(uint8_t* pixels, int width, int height, int stride, float factor) {
    for (int y = 0; y < height; y++) {
        uint8_t* row = pixels + y * stride;
        for (int x = 0; x < width; x++) {
            uint8_t* p = row + x * 4;
            p[0] = static_cast<uint8_t>(std::clamp(p[0] * factor, 0.0f, 255.0f));
            p[1] = static_cast<uint8_t>(std::clamp(p[1] * factor, 0.0f, 255.0f));
            p[2] = static_cast<uint8_t>(std::clamp(p[2] * factor, 0.0f, 255.0f));
        }
    }
}

void FilterPipeline::contrast(uint8_t* pixels, int width, int height, int stride, float factor) {
    float intercept = 128 * (1 - factor);
    for (int y = 0; y < height; y++) {
        uint8_t* row = pixels + y * stride;
        for (int x = 0; x < width; x++) {
            uint8_t* p = row + x * 4;
            p[0] = static_cast<uint8_t>(std::clamp(p[0] * factor + intercept, 0.0f, 255.0f));
            p[1] = static_cast<uint8_t>(std::clamp(p[1] * factor + intercept, 0.0f, 255.0f));
            p[2] = static_cast<uint8_t>(std::clamp(p[2] * factor + intercept, 0.0f, 255.0f));
        }
    }
}

void FilterPipeline::invert(uint8_t* pixels, int width, int height, int stride, float amount) {
    for (int y = 0; y < height; y++) {
        uint8_t* row = pixels + y * stride;
        for (int x = 0; x < width; x++) {
            uint8_t* p = row + x * 4;
            p[0] = static_cast<uint8_t>(std::abs(p[0] - 255 * amount));
            p[1] = static_cast<uint8_t>(std::abs(p[1] - 255 * amount));
            p[2] = static_cast<uint8_t>(std::abs(p[2] - 255 * amount));
        }
    }
}

void FilterPipeline::opacity(uint8_t* pixels, int width, int height, int stride, float amount) {
    for (int y = 0; y < height; y++) {
        uint8_t* row = pixels + y * stride;
        for (int x = 0; x < width; x++) {
            uint8_t* p = row + x * 4;
            p[3] = static_cast<uint8_t>(std::clamp(p[3] * amount, 0.0f, 255.0f));
        }
    }
}

// ==================================================================
// Color matrix filters
// ==================================================================

void FilterPipeline::applyColorMatrix(uint8_t* pixels, int width, int height, int stride,
                                         const float matrix[20]) {
    for (int y = 0; y < height; y++) {
        uint8_t* row = pixels + y * stride;
        for (int x = 0; x < width; x++) {
            uint8_t* p = row + x * 4;
            float r = p[0], g = p[1], b = p[2], a = p[3];

            float nr = matrix[0]*r + matrix[1]*g + matrix[2]*b + matrix[3]*a + matrix[4];
            float ng = matrix[5]*r + matrix[6]*g + matrix[7]*b + matrix[8]*a + matrix[9];
            float nb = matrix[10]*r + matrix[11]*g + matrix[12]*b + matrix[13]*a + matrix[14];
            float na = matrix[15]*r + matrix[16]*g + matrix[17]*b + matrix[18]*a + matrix[19];

            p[0] = static_cast<uint8_t>(std::clamp(nr, 0.0f, 255.0f));
            p[1] = static_cast<uint8_t>(std::clamp(ng, 0.0f, 255.0f));
            p[2] = static_cast<uint8_t>(std::clamp(nb, 0.0f, 255.0f));
            p[3] = static_cast<uint8_t>(std::clamp(na, 0.0f, 255.0f));
        }
    }
}

void FilterPipeline::grayscale(uint8_t* pixels, int width, int height, int stride, float amount) {
    float a = std::clamp(amount, 0.0f, 1.0f);
    // ITU-R BT.601 luma coefficients
    float matrix[20] = {
        0.2126f + 0.7874f * (1-a),  0.7152f - 0.7152f * (1-a),  0.0722f - 0.0722f * (1-a),  0, 0,
        0.2126f - 0.2126f * (1-a),  0.7152f + 0.2848f * (1-a),  0.0722f - 0.0722f * (1-a),  0, 0,
        0.2126f - 0.2126f * (1-a),  0.7152f - 0.7152f * (1-a),  0.0722f + 0.9278f * (1-a),  0, 0,
        0, 0, 0, 1, 0
    };
    applyColorMatrix(pixels, width, height, stride, matrix);
}

void FilterPipeline::sepia(uint8_t* pixels, int width, int height, int stride, float amount) {
    float a = std::clamp(amount, 0.0f, 1.0f);
    // Sepia tone matrix
    float matrix[20] = {
        0.393f + 0.607f * (1-a),  0.769f - 0.769f * (1-a),  0.189f - 0.189f * (1-a),  0, 0,
        0.349f - 0.349f * (1-a),  0.686f + 0.314f * (1-a),  0.168f - 0.168f * (1-a),  0, 0,
        0.272f - 0.272f * (1-a),  0.534f - 0.534f * (1-a),  0.131f + 0.869f * (1-a),  0, 0,
        0, 0, 0, 1, 0
    };
    applyColorMatrix(pixels, width, height, stride, matrix);
}

void FilterPipeline::hueRotate(uint8_t* pixels, int width, int height, int stride, float angleDeg) {
    float rad = angleDeg * static_cast<float>(M_PI) / 180.0f;
    float c = std::cos(rad), s = std::sin(rad);

    // Hue rotation matrix (preserving luminance)
    float matrix[20] = {
        0.213f + c*0.787f - s*0.213f,  0.715f - c*0.715f - s*0.715f,  0.072f - c*0.072f + s*0.928f,  0, 0,
        0.213f - c*0.213f + s*0.143f,  0.715f + c*0.285f + s*0.140f,  0.072f - c*0.072f - s*0.283f,  0, 0,
        0.213f - c*0.213f - s*0.787f,  0.715f - c*0.715f + s*0.715f,  0.072f + c*0.928f + s*0.072f,  0, 0,
        0, 0, 0, 1, 0
    };
    applyColorMatrix(pixels, width, height, stride, matrix);
}

void FilterPipeline::saturate(uint8_t* pixels, int width, int height, int stride, float amount) {
    float a = amount;
    // Saturation matrix using BT.601 luma
    float matrix[20] = {
        0.2126f + 0.7874f * a,  0.7152f - 0.7152f * a,  0.0722f - 0.0722f * a,  0, 0,
        0.2126f - 0.2126f * a,  0.7152f + 0.2848f * a,  0.0722f - 0.0722f * a,  0, 0,
        0.2126f - 0.2126f * a,  0.7152f - 0.7152f * a,  0.0722f + 0.9278f * a,  0, 0,
        0, 0, 0, 1, 0
    };
    applyColorMatrix(pixels, width, height, stride, matrix);
}

void FilterPipeline::dropShadow(uint8_t* pixels, int width, int height, int stride,
                                   float offsetX, float offsetY, float blurRadius, uint32_t color) {
    // Extract alpha channel as shadow mask
    std::vector<uint8_t> shadow(width * height * 4, 0);

    uint8_t sr = (color >> 24) & 0xFF;
    uint8_t sg = (color >> 16) & 0xFF;
    uint8_t sb = (color >> 8) & 0xFF;

    int ox = static_cast<int>(offsetX);
    int oy = static_cast<int>(offsetY);

    // Create shadow from alpha
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int srcX = x - ox;
            int srcY = y - oy;
            if (srcX >= 0 && srcX < width && srcY >= 0 && srcY < height) {
                uint8_t alpha = pixels[srcY * stride + srcX * 4 + 3];
                uint8_t* d = shadow.data() + y * width * 4 + x * 4;
                d[0] = sr; d[1] = sg; d[2] = sb; d[3] = alpha;
            }
        }
    }

    // Blur the shadow
    if (blurRadius > 0) {
        blur(shadow.data(), width, height, width * 4, blurRadius);
    }

    // Composite: shadow behind original (source-over)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t* dst = pixels + y * stride + x * 4;
            uint8_t* src = shadow.data() + y * width * 4 + x * 4;

            float sa = src[3] / 255.0f;
            float da = dst[3] / 255.0f;
            float oa = sa + da * (1 - sa);

            if (oa > 0) {
                dst[0] = static_cast<uint8_t>((src[0] * sa + dst[0] * da * (1 - sa)) / oa);
                dst[1] = static_cast<uint8_t>((src[1] * sa + dst[1] * da * (1 - sa)) / oa);
                dst[2] = static_cast<uint8_t>((src[2] * sa + dst[2] * da * (1 - sa)) / oa);
                dst[3] = static_cast<uint8_t>(oa * 255);
            }
        }
    }
}

} // namespace NXRender
