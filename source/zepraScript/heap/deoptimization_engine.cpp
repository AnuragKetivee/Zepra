// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file deoptimization_engine.cpp
 * @brief Deoptimization: bailing out of optimized JIT code
 *
 * When optimized code becomes invalid (type assumption violated,
 * structure transition, GC-triggered invalidation), the engine
 * must transfer execution from optimized code back to baseline.
 *
 * Steps:
 * 1. Detect invalidation condition (type guard failure, GC event,
 *    structure transition, debugger attach)
 * 2. Find the deopt point: the bytecode offset and stack state
 *    at the point of invalidation
 * 3. Build a "materialized frame": reconstruct the interpreter
 *    stack frame from the optimized frame's register state
 * 4. Patch the return address on the stack to point to a
 *    deopt trampoline (or the interpreter entry point)
 * 5. Resume execution in baseline/interpreter
 *
 * This file provides:
 * - DeoptReason: why deoptimization was triggered
 * - DeoptInfo: mapping from JIT state to interpreter state
 * - FrameMaterializer: reconstructs interpreter frames
 * - DeoptimizationEngine: coordinates the full deopt flow
 */

#include <atomic>
#include <mutex>
#include <vector>
#include <deque>
#include <functional>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Zepra::Heap {

// =============================================================================
// Deoptimization Reasons
// =============================================================================

enum class DeoptReason : uint8_t {
    TypeGuardFailure,       // Speculative type assumption was wrong
    StructureTransition,    // Object shape changed
    BoundsCheckFailure,     // Array bounds check failed
    DivisionByZero,         // Speculative non-zero divisor was wrong
    StackOverflow,          // JIT frame exceeded stack limit
    DebuggerAttached,       // Debugger requested deopt
    GCCodeInvalidation,     // GC collected/moved code dependencies
    UnstableIC,             // Inline cache became megamorphic
    UnexpectedValue,        // Unexpected operand type in specialized op
    LazyDeopt,              // Deferred deopt at safe return
    OSREntry,               // On-stack replacement entry
    OSRExit,                // On-stack replacement exit
};

static const char* deoptReasonName(DeoptReason reason) {
    switch (reason) {
        case DeoptReason::TypeGuardFailure:     return "TypeGuardFailure";
        case DeoptReason::StructureTransition:  return "StructureTransition";
        case DeoptReason::BoundsCheckFailure:   return "BoundsCheckFailure";
        case DeoptReason::DivisionByZero:       return "DivisionByZero";
        case DeoptReason::StackOverflow:        return "StackOverflow";
        case DeoptReason::DebuggerAttached:     return "DebuggerAttached";
        case DeoptReason::GCCodeInvalidation:   return "GCCodeInvalidation";
        case DeoptReason::UnstableIC:           return "UnstableIC";
        case DeoptReason::UnexpectedValue:      return "UnexpectedValue";
        case DeoptReason::LazyDeopt:            return "LazyDeopt";
        case DeoptReason::OSREntry:             return "OSREntry";
        case DeoptReason::OSRExit:              return "OSRExit";
    }
    return "Unknown";
}

// =============================================================================
// Deopt Info (per deopt point in JIT code)
// =============================================================================

/**
 * @brief Describes how to reconstruct interpreter state from JIT state
 *
 * Each speculative guard in JIT code has an associated DeoptInfo
 * that maps the JIT register/stack state back to bytecode-level
 * variables at the corresponding bytecode offset.
 */
struct DeoptInfo {
    uint32_t bytecodeOffset;    // Where to resume in interpreter
    uint32_t functionId;        // Which function

    // Value recovery: maps interpreter local variables to locations
    // in the JIT frame (registers or stack spills)
    struct ValueLocation {
        enum class Kind : uint8_t {
            Register,       // Value is in a machine register
            Stack,          // Value is at a stack offset
            Constant,       // Value is a known constant
            Materialized,   // Value needs to be materialized from sub-ops
            Undefined,      // Local is undefined
        };
        Kind kind;
        union {
            uint8_t registerIndex;      // For Register
            int32_t stackOffset;        // For Stack (from frame pointer)
            int64_t constantValue;      // For Constant
        };
        uint8_t valueType;              // JS type of the value
    };

    std::vector<ValueLocation> locals;   // Local variable mapping
    std::vector<ValueLocation> operands; // Operand stack entries

    // Inline frame chain (for inlined functions)
    struct InlineFrame {
        uint32_t functionId;
        uint32_t bytecodeOffset;
        size_t localsStart;     // Index into locals array
        size_t localsCount;
    };
    std::vector<InlineFrame> inlineFrames;

    bool hasInlines() const { return !inlineFrames.empty(); }
};

// =============================================================================
// Deopt Point Registry
// =============================================================================

/**
 * @brief Maps JIT code offsets to DeoptInfo
 *
 * Each compiled function has a table of deopt points sorted
 * by code offset, enabling binary search from a faulting PC.
 */
class DeoptPointRegistry {
public:
    struct DeoptPoint {
        uintptr_t codeAddress;  // Absolute address in JIT code
        DeoptReason reason;
        DeoptInfo info;
    };

    /**
     * @brief Register deopt points for a function
     */
    void registerPoints(uint32_t functionId,
                        std::vector<DeoptPoint> points) {
        // Sort by code address for binary search
        std::sort(points.begin(), points.end(),
            [](const DeoptPoint& a, const DeoptPoint& b) {
                return a.codeAddress < b.codeAddress;
            });

        std::lock_guard<std::mutex> lock(mutex_);
        pointMap_[functionId] = std::move(points);
    }

    void unregisterPoints(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        pointMap_.erase(functionId);
    }

    /**
     * @brief Find the deopt point for a given PC
     *
     * Returns the deopt point at or just before the given address.
     * This handles the case where the fault occurs inside a
     * guarded instruction sequence.
     */
    const DeoptPoint* findByPC(uintptr_t pc) const {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const auto& [funcId, points] : pointMap_) {
            if (points.empty()) continue;

            // Binary search for the nearest point <= pc
            auto it = std::upper_bound(points.begin(), points.end(), pc,
                [](uintptr_t addr, const DeoptPoint& dp) {
                    return addr < dp.codeAddress;
                });

            if (it != points.begin()) {
                --it;
                // Check if pc is within reasonable range of this deopt point
                if (pc - it->codeAddress < 256) {
                    return &(*it);
                }
            }
        }

        return nullptr;
    }

    size_t totalPoints() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (const auto& [id, pts] : pointMap_) total += pts.size();
        return total;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, std::vector<DeoptPoint>> pointMap_;
};

// =============================================================================
// Frame Materializer
// =============================================================================

/**
 * @brief Reconstructs interpreter frames from JIT frames
 *
 * When deopting, the materializer reads values from the JIT frame
 * (registers and stack spills) and builds an interpreter frame
 * that the interpreter can resume from.
 */
class FrameMaterializer {
public:
    struct MaterializedFrame {
        uint32_t functionId;
        uint32_t bytecodeOffset;
        std::vector<uint64_t> localValues;    // Recovered local variables
        std::vector<uint64_t> operandStack;   // Recovered operand stack
    };

    using RegisterReader = std::function<uint64_t(uint8_t regIndex)>;
    using StackReader = std::function<uint64_t(int32_t offset)>;

    /**
     * @brief Materialize frames from a DeoptInfo
     *
     * For non-inlined code: produces one frame.
     * For inlined code: produces multiple frames (innermost first).
     */
    std::vector<MaterializedFrame> materialize(
        const DeoptInfo& info,
        RegisterReader readReg,
        StackReader readStack
    ) {
        std::vector<MaterializedFrame> frames;

        if (info.hasInlines()) {
            // Produce frames for each inline level
            for (const auto& inlineFrame : info.inlineFrames) {
                MaterializedFrame frame;
                frame.functionId = inlineFrame.functionId;
                frame.bytecodeOffset = inlineFrame.bytecodeOffset;

                // Recover locals for this inline frame
                for (size_t i = 0; i < inlineFrame.localsCount; i++) {
                    size_t idx = inlineFrame.localsStart + i;
                    if (idx < info.locals.size()) {
                        frame.localValues.push_back(
                            recoverValue(info.locals[idx], readReg, readStack));
                    }
                }

                frames.push_back(std::move(frame));
            }
        } else {
            // Single frame
            MaterializedFrame frame;
            frame.functionId = info.functionId;
            frame.bytecodeOffset = info.bytecodeOffset;

            for (const auto& loc : info.locals) {
                frame.localValues.push_back(
                    recoverValue(loc, readReg, readStack));
            }
            for (const auto& loc : info.operands) {
                frame.operandStack.push_back(
                    recoverValue(loc, readReg, readStack));
            }

            frames.push_back(std::move(frame));
        }

        return frames;
    }

private:
    uint64_t recoverValue(
        const DeoptInfo::ValueLocation& loc,
        RegisterReader& readReg,
        StackReader& readStack
    ) {
        switch (loc.kind) {
            case DeoptInfo::ValueLocation::Kind::Register:
                return readReg(loc.registerIndex);

            case DeoptInfo::ValueLocation::Kind::Stack:
                return readStack(loc.stackOffset);

            case DeoptInfo::ValueLocation::Kind::Constant:
                return static_cast<uint64_t>(loc.constantValue);

            case DeoptInfo::ValueLocation::Kind::Undefined:
                return 0;  // Undefined tag

            case DeoptInfo::ValueLocation::Kind::Materialized:
                // Complex materialization (e.g. inlined object creation)
                // would need additional logic here
                return 0;
        }
        return 0;
    }
};

// =============================================================================
// Deoptimization Engine
// =============================================================================

class DeoptimizationEngine {
public:
    struct Stats {
        std::atomic<uint64_t> totalDeopts{0};
        std::atomic<uint64_t> typeGuardDeopts{0};
        std::atomic<uint64_t> structureDeopts{0};
        std::atomic<uint64_t> gcDeopts{0};
        std::atomic<uint64_t> debuggerDeopts{0};
        std::atomic<uint64_t> osrEntries{0};
        std::atomic<uint64_t> osrExits{0};
        std::atomic<uint64_t> framesRematerialized{0};
    };

    struct DeoptResult {
        bool success;
        uint32_t functionId;
        uint32_t bytecodeOffset;
        DeoptReason reason;
        size_t framesRecovered;
        double durationUs;
    };

    // Callbacks from the VM
    struct Callbacks {
        // Patch return address on stack frame to redirect to interpreter
        std::function<void(uintptr_t frameAddr, uintptr_t newRetAddr)>
            patchReturnAddress;

        // Install materialized frame into the interpreter
        std::function<void(const FrameMaterializer::MaterializedFrame&)>
            installFrame;

        // Get interpreter entry point for a function
        std::function<uintptr_t(uint32_t functionId, uint32_t bytecodeOffset)>
            getInterpreterEntry;

        // Invalidate an optimized function (prevent re-entry)
        std::function<void(uint32_t functionId)> invalidateCode;

        // Read register from trapped frame
        FrameMaterializer::RegisterReader readRegister;

        // Read stack from trapped frame
        FrameMaterializer::StackReader readStack;
    };

    DeoptimizationEngine() = default;

    void setCallbacks(Callbacks callbacks) { cb_ = std::move(callbacks); }
    DeoptPointRegistry& registry() { return registry_; }

    /**
     * @brief Trigger deoptimization at a given PC
     *
     * Called when:
     * - A type guard fails (immediate deopt)
     * - GC invalidates code (lazy deopt at next return)
     * - Debugger attaches
     */
    DeoptResult deoptimize(uintptr_t faultPC, DeoptReason reason) {
        DeoptResult result{};
        auto start = std::chrono::steady_clock::now();

        result.reason = reason;
        stats_.totalDeopts.fetch_add(1, std::memory_order_relaxed);
        trackReasonStats(reason);

        // Find deopt info
        auto* deoptPoint = registry_.findByPC(faultPC);
        if (!deoptPoint) {
            fprintf(stderr, "[deopt] No deopt point for PC 0x%lx\n",
                static_cast<unsigned long>(faultPC));
            result.success = false;
            result.durationUs = elapsed(start);
            return result;
        }

        result.functionId = deoptPoint->info.functionId;
        result.bytecodeOffset = deoptPoint->info.bytecodeOffset;

        // Materialize interpreter frames
        auto frames = materializer_.materialize(
            deoptPoint->info, cb_.readRegister, cb_.readStack);

        result.framesRecovered = frames.size();
        stats_.framesRematerialized.fetch_add(
            frames.size(), std::memory_order_relaxed);

        // Install frames in interpreter
        for (auto& frame : frames) {
            if (cb_.installFrame) {
                cb_.installFrame(frame);
            }
        }

        // Get interpreter entry point
        uintptr_t interpEntry = 0;
        if (cb_.getInterpreterEntry) {
            interpEntry = cb_.getInterpreterEntry(
                result.functionId, result.bytecodeOffset);
        }

        // Patch return address to redirect to interpreter
        if (cb_.patchReturnAddress && interpEntry != 0) {
            cb_.patchReturnAddress(faultPC, interpEntry);
        }

        // Invalidate the optimized code
        if (cb_.invalidateCode) {
            cb_.invalidateCode(result.functionId);
        }

        result.success = true;
        result.durationUs = elapsed(start);

        return result;
    }

    /**
     * @brief Lazy deopt: mark a function for deoptimization
     *
     * Used when GC discovers that code dependencies are stale.
     * The function will deopt when it next returns (at a safe point).
     */
    void scheduleLazyDeopt(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(lazyMutex_);
        lazyDeoptSet_.insert(functionId);
    }

    bool isLazyDeoptScheduled(uint32_t functionId) const {
        std::lock_guard<std::mutex> lock(lazyMutex_);
        return lazyDeoptSet_.count(functionId) > 0;
    }

    void clearLazyDeopt(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(lazyMutex_);
        lazyDeoptSet_.erase(functionId);
    }

    /**
     * @brief Check at function return if lazy deopt is pending
     *
     * Called by the JIT's return sequence to check if deopt
     * was requested while the function was executing.
     */
    bool checkLazyDeoptAtReturn(uint32_t functionId, uintptr_t returnPC) {
        if (!isLazyDeoptScheduled(functionId)) return false;

        clearLazyDeopt(functionId);
        deoptimize(returnPC, DeoptReason::LazyDeopt);
        return true;
    }

    /**
     * @brief Per-reason deopt frequency (for re-compilation heuristics)
     *
     * If a function deopts too often for the same reason, the JIT
     * should stop recompiling with that speculation.
     */
    struct DeoptHistory {
        uint32_t functionId;
        DeoptReason reason;
        uint32_t count;
    };

    void recordDeopt(uint32_t functionId, DeoptReason reason) {
        std::lock_guard<std::mutex> lock(historyMutex_);
        auto key = (static_cast<uint64_t>(functionId) << 8) |
                   static_cast<uint8_t>(reason);
        deoptCounts_[key]++;
    }

    uint32_t deoptCount(uint32_t functionId, DeoptReason reason) const {
        std::lock_guard<std::mutex> lock(historyMutex_);
        auto key = (static_cast<uint64_t>(functionId) << 8) |
                   static_cast<uint8_t>(reason);
        auto it = deoptCounts_.find(key);
        return it != deoptCounts_.end() ? it->second : 0;
    }

    /**
     * @brief Should this function be recompiled with the given speculation?
     *
     * Returns false if the function has deopted too many times
     * due to the same speculation.
     */
    bool shouldRecompile(uint32_t functionId, DeoptReason prevReason,
                         uint32_t threshold = 5) const {
        return deoptCount(functionId, prevReason) < threshold;
    }

    const Stats& stats() const { return stats_; }

private:
    void trackReasonStats(DeoptReason reason) {
        switch (reason) {
            case DeoptReason::TypeGuardFailure:
                stats_.typeGuardDeopts.fetch_add(1, std::memory_order_relaxed);
                break;
            case DeoptReason::StructureTransition:
                stats_.structureDeopts.fetch_add(1, std::memory_order_relaxed);
                break;
            case DeoptReason::GCCodeInvalidation:
                stats_.gcDeopts.fetch_add(1, std::memory_order_relaxed);
                break;
            case DeoptReason::DebuggerAttached:
                stats_.debuggerDeopts.fetch_add(1, std::memory_order_relaxed);
                break;
            case DeoptReason::OSREntry:
                stats_.osrEntries.fetch_add(1, std::memory_order_relaxed);
                break;
            case DeoptReason::OSRExit:
                stats_.osrExits.fetch_add(1, std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }

    static double elapsed(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - start).count();
    }

    Callbacks cb_;
    DeoptPointRegistry registry_;
    FrameMaterializer materializer_;
    Stats stats_;

    mutable std::mutex lazyMutex_;
    std::unordered_set<uint32_t> lazyDeoptSet_;

    mutable std::mutex historyMutex_;
    std::unordered_map<uint64_t, uint32_t> deoptCounts_;
};

} // namespace Zepra::Heap
