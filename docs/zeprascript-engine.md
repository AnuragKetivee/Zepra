# ZebraScript JavaScript Engine

> **Status:** Development | **Tests:** 123 ✅ | **ECMAScript Target:** ES2024

---

## Overview

ZebraScript is a custom JavaScript engine for ZepraBrowser, designed to be **vendor-independent** with no reliance on V8, SpiderMonkey, or JavaScriptCore.

---

## Engine Comparison

| Feature | ZebraScript | SpiderMonkey (Firefox) | JavaScriptCore (WebKit) |
|---------|-------------|------------------------|-------------------------|
| **Test262 Coverage** | ~17 basic tests | 50,000+ | 52,000+ |
| **JIT Tiers** | Planned (0-3) | 4 tiers (Baseline→Ion) | 4 tiers (LLInt→FTL) |
| **GC Type** | Generational (planned) | Generational/Incremental | Generational |
| **Parser** | Recursive descent | Hand-written | Hand-written |
| **Development Status** | Alpha | Production | Production |

---

## Current Test Status

### Unit Tests: **123/123** ✅

| Suite | Tests | Status |
|-------|-------|--------|
| LexerTests | 10 | ✅ |
| ParserTests | 21 | ✅ |
| ValueTypeTests | 10 | ✅ |
| ArithmeticTests | 12 | ✅ |
| EqualityTests | 8 | ✅ |
| ArrayTests | 10 | ✅ |
| BuiltinTests | 8 | ✅ |
| URLTests | 13 | ✅ |
| Others | 31 | ✅ |

### ECMAScript Compliance

| Category | Status |
|----------|--------|
| Literals (number, string, boolean) | ✅ |
| Operators (arithmetic, comparison, logical) | ✅ |
| Control Flow (if, while, for, for-of) | ✅ |
| Functions (declaration, arrow, async) | ✅ |
| Objects & Arrays | ✅ |
| Classes | ✅ |
| Modules (import/export) | ✅ |
| Try/Catch | ✅ |
| Async/Await | ✅ |
| Destructuring | 🔄 |
| Generators | ⏳ |
| Symbols | ⏳ |
| Proxies | ⏳ |

---

## Recent Fixes

### Parser Infinite Loop Bugs (Dec 2024)

Fixed 5 critical parser bugs causing infinite loops:

1. **Lexer Backtracking** - Added `LexerState` checkpoint/restore
2. **Arrow Functions** - Proper state restore in `parseAssignmentExpression`
3. **Async Functions** - Added `async function` support in `parseDeclaration`
4. **For Loops** - Fixed C-style and for-of parsing
5. **Rest Parameters** - Fixed `...args` parsing

### Value Type Bugs (Dec 2024)

- Fixed `undefined.isNumber()` returning `true` (TAG_UNDEFINED collision)
- Fixed `NaN === NaN` returning `true` (should be `false` per ECMAScript)

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    ZebraScript                          │
├─────────────────────────────────────────────────────────┤
│  Frontend         │  Runtime           │  Builtins      │
│  - Lexer          │  - VM              │  - Array       │
│  - Parser         │  - GC (planned)    │  - String      │
│  - AST            │  - Value (NaN-box) │  - Math        │
│  - Bytecode       │  - Object          │  - JSON        │
├─────────────────────────────────────────────────────────┤
│  JIT (Planned)    │  Browser APIs      │  Debug         │
│  - Tier 0-3       │  - DOM             │  - DevTools    │
│  - IC Caching     │  - Fetch           │  - Profiler    │
└─────────────────────────────────────────────────────────┘
```

---

## Building & Testing

```bash
# Build
cd apps/zeprabrowser/build
ninja zepra-unit-tests

# Run all tests
./bin/zepra-unit-tests

# Run specific suite
./bin/zepra-unit-tests --gtest_filter="ParserTests.*"
```

---

## Roadmap

| Phase | Target | Status |
|-------|--------|--------|
| 1. Core Parser | ES2015 syntax | ✅ |
| 2. Value System | NaN-boxing | ✅ |
| 3. Builtins | Array, String, Math | ✅ |
| 4. Garbage Collector | Generational | ✅ |
| 5. JIT Compilation | Tier 0-3 | 🔄 (28/80 opcodes) |
| 6. Full ES2024 | Complete spec | ⏳ |

---

*Last Updated: December 2024*
