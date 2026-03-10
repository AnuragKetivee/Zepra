// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — regexp_builtins.cpp — RegExp execution, flags, named groups

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace Zepra::Runtime {

struct RegExpFlags {
    bool global;        // g
    bool ignoreCase;    // i
    bool multiline;     // m
    bool dotAll;        // s
    bool unicode;       // u
    bool unicodeSets;   // v (ES2024)
    bool sticky;        // y
    bool hasIndices;    // d (ES2022)

    RegExpFlags() : global(false), ignoreCase(false), multiline(false), dotAll(false)
        , unicode(false), unicodeSets(false), sticky(false), hasIndices(false) {}

    static RegExpFlags parse(const std::string& flagStr) {
        RegExpFlags f;
        for (char c : flagStr) {
            switch (c) {
                case 'g': f.global = true; break;
                case 'i': f.ignoreCase = true; break;
                case 'm': f.multiline = true; break;
                case 's': f.dotAll = true; break;
                case 'u': f.unicode = true; break;
                case 'v': f.unicodeSets = true; break;
                case 'y': f.sticky = true; break;
                case 'd': f.hasIndices = true; break;
            }
        }
        return f;
    }

    std::string toString() const {
        std::string result;
        if (hasIndices) result += 'd';
        if (global) result += 'g';
        if (ignoreCase) result += 'i';
        if (multiline) result += 'm';
        if (dotAll) result += 's';
        if (unicode) result += 'u';
        if (unicodeSets) result += 'v';
        if (sticky) result += 'y';
        return result;
    }
};

struct MatchResult {
    bool matched;
    size_t index;              // Start position of match
    std::string fullMatch;
    std::vector<std::string> captures;    // Capture groups
    std::unordered_map<std::string, std::string> namedCaptures;
    std::vector<std::pair<size_t, size_t>> indices;  // [start, end] for each group

    MatchResult() : matched(false), index(0) {}
};

struct CompiledPattern {
    std::string source;
    RegExpFlags flags;
    uint32_t captureCount;
    size_t lastIndex;

    // Compiled NFA state table.
    struct State {
        enum class Kind : uint8_t {
            Literal,       // Match exact character
            CharClass,     // Match character class [abc]
            AnyChar,       // Match . (any char)
            Assertion,     // ^ $ \b
            Group,         // Capture group
            Quantifier,    // * + ? {n,m}
            Alternation,   // |
            Backreference, // \1
            Lookahead,     // (?=...) (?!...)
            Lookbehind,    // (?<=...) (?<!...)
            Accept,        // Match accepted
        };

        Kind kind;
        char literal;
        uint32_t next;        // Next state index
        uint32_t alt;         // Alternative state (for branches)
        uint32_t groupId;
        bool negated;         // For negative lookahead/lookbehind
        uint32_t quantMin;
        uint32_t quantMax;
        bool greedy;

        State() : kind(Kind::Literal), literal(0), next(0), alt(0)
            , groupId(0), negated(false), quantMin(1), quantMax(1), greedy(true) {}
    };

    std::vector<State> states;

    CompiledPattern() : captureCount(0), lastIndex(0) {}
};

class RegExpBuiltins {
public:
    // Compile a pattern.
    bool compile(const std::string& source, const std::string& flags,
                 CompiledPattern& pattern) {
        pattern.source = source;
        pattern.flags = RegExpFlags::parse(flags);
        pattern.captureCount = 0;
        pattern.lastIndex = 0;
        pattern.states.clear();

        return compilePattern(source, pattern);
    }

    // RegExp.prototype.exec
    MatchResult exec(CompiledPattern& pattern, const std::string& input) {
        MatchResult result;
        size_t startIdx = pattern.flags.sticky ? pattern.lastIndex : 0;

        for (size_t i = startIdx; i <= input.length(); i++) {
            result = matchAt(pattern, input, i);
            if (result.matched) {
                if (pattern.flags.global || pattern.flags.sticky) {
                    pattern.lastIndex = result.index + result.fullMatch.length();
                }
                return result;
            }
            if (pattern.flags.sticky) break;
        }

        if (pattern.flags.global || pattern.flags.sticky) {
            pattern.lastIndex = 0;
        }
        return result;
    }

    // RegExp.prototype.test
    bool test(CompiledPattern& pattern, const std::string& input) {
        return exec(pattern, input).matched;
    }

    // String.prototype.match (non-global)
    MatchResult match(CompiledPattern& pattern, const std::string& input) {
        if (!pattern.flags.global) return exec(pattern, input);

        // Global: collect all matches.
        pattern.lastIndex = 0;
        MatchResult combined;
        combined.matched = false;

        while (true) {
            MatchResult m = exec(pattern, input);
            if (!m.matched) break;
            combined.matched = true;
            combined.captures.push_back(m.fullMatch);
            if (m.fullMatch.empty()) pattern.lastIndex++;
        }
        return combined;
    }

    // RegExp.prototype[Symbol.replace]
    std::string replace(CompiledPattern& pattern, const std::string& input,
                        const std::string& replacement) {
        if (!pattern.flags.global) {
            MatchResult m = exec(pattern, input);
            if (!m.matched) return input;
            return input.substr(0, m.index) +
                   applyReplacement(replacement, m) +
                   input.substr(m.index + m.fullMatch.length());
        }

        // Global replace.
        pattern.lastIndex = 0;
        std::string result;
        size_t lastPos = 0;

        while (true) {
            MatchResult m = exec(pattern, input);
            if (!m.matched) break;
            result += input.substr(lastPos, m.index - lastPos);
            result += applyReplacement(replacement, m);
            lastPos = m.index + m.fullMatch.length();
            if (m.fullMatch.empty()) {
                if (lastPos < input.length()) result += input[lastPos++];
                else break;
            }
        }
        result += input.substr(lastPos);
        return result;
    }

private:
    bool compilePattern(const std::string& source, CompiledPattern& pattern) {
        // Simplified compilation: build literal-matching NFA.
        for (size_t i = 0; i < source.length(); i++) {
            CompiledPattern::State state;

            switch (source[i]) {
                case '.':
                    state.kind = CompiledPattern::State::Kind::AnyChar;
                    break;
                case '^':
                    state.kind = CompiledPattern::State::Kind::Assertion;
                    state.literal = '^';
                    break;
                case '$':
                    state.kind = CompiledPattern::State::Kind::Assertion;
                    state.literal = '$';
                    break;
                case '\\':
                    if (i + 1 < source.length()) {
                        i++;
                        state.kind = CompiledPattern::State::Kind::Literal;
                        state.literal = source[i];
                    }
                    break;
                case '(':
                    state.kind = CompiledPattern::State::Kind::Group;
                    state.groupId = ++pattern.captureCount;
                    break;
                case ')':
                    continue;  // Close group
                default:
                    state.kind = CompiledPattern::State::Kind::Literal;
                    state.literal = source[i];
                    break;
            }

            state.next = static_cast<uint32_t>(pattern.states.size() + 1);
            pattern.states.push_back(state);
        }

        // Accept state.
        CompiledPattern::State accept;
        accept.kind = CompiledPattern::State::Kind::Accept;
        pattern.states.push_back(accept);
        return true;
    }

    MatchResult matchAt(const CompiledPattern& pattern, const std::string& input,
                         size_t pos) {
        MatchResult result;
        size_t stateIdx = 0;
        size_t startPos = pos;
        std::vector<std::string> captures(pattern.captureCount + 1);

        while (stateIdx < pattern.states.size()) {
            const auto& state = pattern.states[stateIdx];

            switch (state.kind) {
                case CompiledPattern::State::Kind::Accept:
                    result.matched = true;
                    result.index = startPos;
                    result.fullMatch = input.substr(startPos, pos - startPos);
                    result.captures = captures;
                    return result;

                case CompiledPattern::State::Kind::Literal:
                    if (pos >= input.length()) return result;
                    if (pattern.flags.ignoreCase) {
                        if (tolower(input[pos]) != tolower(state.literal)) return result;
                    } else {
                        if (input[pos] != state.literal) return result;
                    }
                    pos++;
                    stateIdx++;
                    break;

                case CompiledPattern::State::Kind::AnyChar:
                    if (pos >= input.length()) return result;
                    if (!pattern.flags.dotAll && input[pos] == '\n') return result;
                    pos++;
                    stateIdx++;
                    break;

                case CompiledPattern::State::Kind::Assertion:
                    if (state.literal == '^' && pos != 0) {
                        if (!pattern.flags.multiline || input[pos - 1] != '\n') return result;
                    }
                    if (state.literal == '$' && pos != input.length()) {
                        if (!pattern.flags.multiline ||
                            (pos < input.length() && input[pos] != '\n')) return result;
                    }
                    stateIdx++;
                    break;

                case CompiledPattern::State::Kind::Group:
                    stateIdx++;
                    break;

                default:
                    stateIdx++;
                    break;
            }
        }
        return result;
    }

    std::string applyReplacement(const std::string& replacement, const MatchResult& match) {
        std::string result;
        for (size_t i = 0; i < replacement.length(); i++) {
            if (replacement[i] == '$' && i + 1 < replacement.length()) {
                char next = replacement[i + 1];
                if (next == '&') { result += match.fullMatch; i++; continue; }
                if (next == '`') { i++; continue; }  // Before match
                if (next == '\'') { i++; continue; }  // After match
                if (next >= '1' && next <= '9') {
                    size_t idx = next - '0';
                    if (idx < match.captures.size()) result += match.captures[idx];
                    i++;
                    continue;
                }
            }
            result += replacement[i];
        }
        return result;
    }
};

} // namespace Zepra::Runtime
