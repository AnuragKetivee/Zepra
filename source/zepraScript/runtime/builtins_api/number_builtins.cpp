// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — number_builtins.cpp — Number built-in methods

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <limits>
#include <algorithm>

namespace Zepra::Runtime {

class NumberBuiltins {
public:
    static bool isFinite(double n) {
        return std::isfinite(n);
    }

    static bool isNaN(double n) {
        return std::isnan(n);
    }

    static bool isInteger(double n) {
        return std::isfinite(n) && n == std::floor(n);
    }

    static bool isSafeInteger(double n) {
        return isInteger(n) && std::abs(n) <= 9007199254740991.0;
    }

    static double parseFloat(const char* str) {
        if (!str) return std::numeric_limits<double>::quiet_NaN();
        while (*str == ' ' || *str == '\t' || *str == '\n') str++;
        char* end = nullptr;
        double result = strtod(str, &end);
        if (end == str) return std::numeric_limits<double>::quiet_NaN();
        return result;
    }

    static int64_t parseInt(const char* str, int radix = 10) {
        if (!str) return 0;
        while (*str == ' ' || *str == '\t' || *str == '\n') str++;
        bool negative = false;
        if (*str == '-') { negative = true; str++; }
        else if (*str == '+') { str++; }

        if (radix == 0 || radix == 16) {
            if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
                str += 2;
                radix = 16;
            } else if (radix == 0) {
                radix = 10;
            }
        }
        if (radix < 2 || radix > 36) return 0;

        int64_t result = 0;
        bool parsed = false;
        while (*str) {
            int digit = -1;
            if (*str >= '0' && *str <= '9') digit = *str - '0';
            else if (*str >= 'a' && *str <= 'z') digit = *str - 'a' + 10;
            else if (*str >= 'A' && *str <= 'Z') digit = *str - 'A' + 10;
            if (digit < 0 || digit >= radix) break;
            result = result * radix + digit;
            parsed = true;
            str++;
        }
        if (!parsed) return 0;
        return negative ? -result : result;
    }

    // Number.prototype.toFixed
    static std::string toFixed(double n, int digits) {
        if (digits < 0 || digits > 100) return "";
        if (std::isnan(n)) return "NaN";
        if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
        char fmt[16];
        snprintf(fmt, sizeof(fmt), "%%.%df", digits);
        char buf[128];
        snprintf(buf, sizeof(buf), fmt, n);
        return buf;
    }

    // Number.prototype.toPrecision
    static std::string toPrecision(double n, int precision) {
        if (precision < 1 || precision > 100) return "";
        if (std::isnan(n)) return "NaN";
        if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
        char fmt[16];
        snprintf(fmt, sizeof(fmt), "%%.%dg", precision);
        char buf[128];
        snprintf(buf, sizeof(buf), fmt, n);
        return buf;
    }

    // Number.prototype.toExponential
    static std::string toExponential(double n, int digits) {
        if (std::isnan(n)) return "NaN";
        if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
        char fmt[16];
        snprintf(fmt, sizeof(fmt), "%%.%de", digits);
        char buf[128];
        snprintf(buf, sizeof(buf), fmt, n);
        return buf;
    }

    // Number.prototype.toString
    static std::string toString(double n, int radix = 10) {
        if (std::isnan(n)) return "NaN";
        if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
        if (n == 0.0) return "0";
        if (radix == 10) {
            if (n == std::floor(n) && std::abs(n) < 1e15) {
                return std::to_string(static_cast<int64_t>(n));
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%.17g", n);
            return buf;
        }

        // Non-decimal radix.
        bool negative = n < 0;
        int64_t intPart = static_cast<int64_t>(std::abs(n));
        std::string result;
        if (intPart == 0) {
            result = "0";
        } else {
            while (intPart > 0) {
                int digit = intPart % radix;
                result += digit < 10 ? static_cast<char>('0' + digit)
                                     : static_cast<char>('a' + digit - 10);
                intPart /= radix;
            }
            std::reverse(result.begin(), result.end());
        }
        return negative ? "-" + result : result;
    }

    // Constants.
    static constexpr double EPSILON = 2.220446049250313e-16;
    static constexpr double MAX_VALUE = 1.7976931348623157e+308;
    static constexpr double MIN_VALUE = 5e-324;
    static constexpr double MAX_SAFE_INTEGER = 9007199254740991.0;
    static constexpr double MIN_SAFE_INTEGER = -9007199254740991.0;
    static constexpr double POSITIVE_INFINITY = std::numeric_limits<double>::infinity();
    static constexpr double NEGATIVE_INFINITY = -std::numeric_limits<double>::infinity();
    static constexpr double NaN_VALUE = std::numeric_limits<double>::quiet_NaN();
};

} // namespace Zepra::Runtime
