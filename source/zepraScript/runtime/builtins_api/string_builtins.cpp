// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — string_builtins.cpp — String.prototype methods

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <functional>

namespace Zepra::Runtime {

class StringBuiltins {
public:
    // String.prototype.replace
    static std::string replace(const std::string& str, const std::string& search,
                               const std::string& replacement) {
        size_t pos = str.find(search);
        if (pos == std::string::npos) return str;
        std::string result = str;
        result.replace(pos, search.length(), replacement);
        return result;
    }

    // String.prototype.replaceAll (ES2021)
    static std::string replaceAll(const std::string& str, const std::string& search,
                                   const std::string& replacement) {
        if (search.empty()) return str;
        std::string result;
        size_t pos = 0;
        size_t prev = 0;
        while ((pos = str.find(search, prev)) != std::string::npos) {
            result.append(str, prev, pos - prev);
            result.append(replacement);
            prev = pos + search.length();
        }
        result.append(str, prev, str.length() - prev);
        return result;
    }

    // String.prototype.split
    static std::vector<std::string> split(const std::string& str,
                                           const std::string& separator,
                                           int32_t limit = -1) {
        std::vector<std::string> result;
        if (separator.empty()) {
            for (char c : str) {
                if (limit >= 0 && static_cast<int32_t>(result.size()) >= limit) break;
                result.push_back(std::string(1, c));
            }
            return result;
        }

        size_t pos = 0;
        size_t prev = 0;
        while ((pos = str.find(separator, prev)) != std::string::npos) {
            if (limit >= 0 && static_cast<int32_t>(result.size()) >= limit) break;
            result.push_back(str.substr(prev, pos - prev));
            prev = pos + separator.length();
        }
        if (limit < 0 || static_cast<int32_t>(result.size()) < limit) {
            result.push_back(str.substr(prev));
        }
        return result;
    }

    // String.prototype.slice
    static std::string slice(const std::string& str, int32_t start, int32_t end = INT32_MAX) {
        int32_t len = static_cast<int32_t>(str.length());
        if (start < 0) start = std::max(len + start, 0);
        if (end < 0) end = std::max(len + end, 0);
        if (end == INT32_MAX) end = len;
        start = std::min(start, len);
        end = std::min(end, len);
        if (start >= end) return "";
        return str.substr(start, end - start);
    }

    // String.prototype.substring
    static std::string substring(const std::string& str, int32_t start, int32_t end = -1) {
        int32_t len = static_cast<int32_t>(str.length());
        if (start < 0) start = 0;
        if (end < 0) end = len;
        start = std::min(start, len);
        end = std::min(end, len);
        if (start > end) std::swap(start, end);
        return str.substr(start, end - start);
    }

    // String.prototype.padStart (ES2017)
    static std::string padStart(const std::string& str, size_t targetLength,
                                 const std::string& padStr = " ") {
        if (str.length() >= targetLength || padStr.empty()) return str;
        size_t padLen = targetLength - str.length();
        std::string padding;
        while (padding.length() < padLen) padding += padStr;
        return padding.substr(0, padLen) + str;
    }

    // String.prototype.padEnd (ES2017)
    static std::string padEnd(const std::string& str, size_t targetLength,
                               const std::string& padStr = " ") {
        if (str.length() >= targetLength || padStr.empty()) return str;
        size_t padLen = targetLength - str.length();
        std::string padding;
        while (padding.length() < padLen) padding += padStr;
        return str + padding.substr(0, padLen);
    }

    // String.prototype.trim
    static std::string trim(const std::string& str) {
        return trimEnd(trimStart(str));
    }

    // String.prototype.trimStart (ES2019)
    static std::string trimStart(const std::string& str) {
        size_t start = 0;
        while (start < str.length() && isWhitespace(str[start])) start++;
        return str.substr(start);
    }

    // String.prototype.trimEnd (ES2019)
    static std::string trimEnd(const std::string& str) {
        size_t end = str.length();
        while (end > 0 && isWhitespace(str[end - 1])) end--;
        return str.substr(0, end);
    }

    // String.prototype.startsWith
    static bool startsWith(const std::string& str, const std::string& search,
                           size_t position = 0) {
        if (position + search.length() > str.length()) return false;
        return str.compare(position, search.length(), search) == 0;
    }

    // String.prototype.endsWith
    static bool endsWith(const std::string& str, const std::string& search,
                         size_t endPos = SIZE_MAX) {
        if (endPos > str.length()) endPos = str.length();
        if (search.length() > endPos) return false;
        return str.compare(endPos - search.length(), search.length(), search) == 0;
    }

    // String.prototype.includes
    static bool includes(const std::string& str, const std::string& search,
                         size_t position = 0) {
        return str.find(search, position) != std::string::npos;
    }

    // String.prototype.repeat (ES2015)
    static std::string repeat(const std::string& str, size_t count) {
        if (count == 0 || str.empty()) return "";
        std::string result;
        result.reserve(str.length() * count);
        for (size_t i = 0; i < count; i++) result += str;
        return result;
    }

    // String.prototype.at (ES2022)
    static std::string at(const std::string& str, int32_t index) {
        int32_t len = static_cast<int32_t>(str.length());
        int32_t actual = index < 0 ? len + index : index;
        if (actual < 0 || actual >= len) return "";
        return std::string(1, str[actual]);
    }

    // String.prototype.toUpperCase / toLowerCase
    static std::string toUpperCase(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }

    static std::string toLowerCase(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    // String.prototype.charAt
    static std::string charAt(const std::string& str, size_t index) {
        if (index >= str.length()) return "";
        return std::string(1, str[index]);
    }

    // String.prototype.charCodeAt
    static int32_t charCodeAt(const std::string& str, size_t index) {
        if (index >= str.length()) return -1;
        return static_cast<int32_t>(static_cast<uint8_t>(str[index]));
    }

    // String.prototype.concat
    static std::string concat(const std::string& str,
                               const std::vector<std::string>& args) {
        std::string result = str;
        for (auto& s : args) result += s;
        return result;
    }

    // String.fromCharCode
    static std::string fromCharCode(const std::vector<int32_t>& codes) {
        std::string result;
        for (int32_t code : codes) {
            if (code >= 0 && code <= 0x7F) {
                result += static_cast<char>(code);
            }
        }
        return result;
    }

    // String.prototype.matchAll (ES2020) — returns match positions.
    static std::vector<std::pair<size_t, std::string>> matchAll(
        const std::string& str, const std::string& pattern) {
        std::vector<std::pair<size_t, std::string>> matches;
        size_t pos = 0;
        while ((pos = str.find(pattern, pos)) != std::string::npos) {
            matches.push_back({pos, pattern});
            pos += pattern.length();
            if (pattern.empty()) pos++;
        }
        return matches;
    }

private:
    static bool isWhitespace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r'
            || c == '\f' || c == '\v';
    }
};

} // namespace Zepra::Runtime
