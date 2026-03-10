// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — jit_tier_policy.cpp — Tiering decisions: interp→baseline→optimizing

#include <cstdint>
#include <cassert>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <chrono>

namespace Zepra::JIT {

enum class Tier : uint8_t {
    Interpreter,
    Baseline,
    Optimized,
};

struct FunctionProfile {
    uint32_t functionId;
    Tier currentTier;
    uint64_t callCount;
    uint64_t loopIterations;
    uint32_t deoptCount;
    uint32_t deoptBudget;         // Max deopts before giving up on optimization
    uint32_t bytecodeSize;
    double lastCompileTimeMs;
    bool markedForCompile;
    bool blacklisted;             // Too many deopts

    FunctionProfile() : functionId(0), currentTier(Tier::Interpreter)
        , callCount(0), loopIterations(0), deoptCount(0), deoptBudget(10)
        , bytecodeSize(0), lastCompileTimeMs(0)
        , markedForCompile(false), blacklisted(false) {}
};

struct TierConfig {
    uint64_t baselineCallThreshold;       // Calls before baseline compile
    uint64_t optimizingCallThreshold;     // Calls before optimizing compile
    uint64_t baselineLoopThreshold;       // Loop iterations for OSR baseline
    uint64_t optimizingLoopThreshold;     // Loop iterations for OSR optimized
    uint32_t maxDeoptCount;               // Max deopts before blacklist
    uint32_t maxBytecodeSizeForInline;    // Max bytecode size for inlining
    double maxCompileTimeBudgetMs;        // Max time per compile
    bool enableOSR;                       // On-stack replacement

    TierConfig() : baselineCallThreshold(100), optimizingCallThreshold(1000)
        , baselineLoopThreshold(200), optimizingLoopThreshold(5000)
        , maxDeoptCount(10), maxBytecodeSizeForInline(200)
        , maxCompileTimeBudgetMs(50.0), enableOSR(true) {}
};

class TierPolicy {
public:
    TierPolicy() {}

    void setConfig(TierConfig config) { config_ = config; }

    // Record a function call.
    void recordCall(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& profile = profiles_[functionId];
        profile.functionId = functionId;
        profile.callCount++;
    }

    // Record loop back-edge (for OSR).
    void recordLoopIteration(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& profile = profiles_[functionId];
        profile.loopIterations++;
    }

    // Record deoptimization.
    void recordDeopt(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& profile = profiles_[functionId];
        profile.deoptCount++;
        if (profile.deoptCount >= config_.maxDeoptCount) {
            profile.blacklisted = true;
            profile.currentTier = Tier::Baseline;  // Demote
        }
    }

    // Check if function should be compiled to next tier.
    Tier shouldCompile(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = profiles_.find(functionId);
        if (it == profiles_.end()) return Tier::Interpreter;

        auto& p = it->second;
        if (p.blacklisted) return p.currentTier;

        switch (p.currentTier) {
            case Tier::Interpreter:
                if (p.callCount >= config_.baselineCallThreshold ||
                    (config_.enableOSR && p.loopIterations >= config_.baselineLoopThreshold)) {
                    p.markedForCompile = true;
                    return Tier::Baseline;
                }
                break;

            case Tier::Baseline:
                if (p.callCount >= config_.optimizingCallThreshold ||
                    (config_.enableOSR && p.loopIterations >= config_.optimizingLoopThreshold)) {
                    if (p.deoptCount < config_.maxDeoptCount) {
                        p.markedForCompile = true;
                        return Tier::Optimized;
                    }
                }
                break;

            case Tier::Optimized:
                break;
        }
        return p.currentTier;
    }

    // Notify compilation complete.
    void compilationDone(uint32_t functionId, Tier tier, double compileTimeMs) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& profile = profiles_[functionId];
        profile.currentTier = tier;
        profile.markedForCompile = false;
        profile.lastCompileTimeMs = compileTimeMs;

        stats_.totalCompilations++;
        stats_.totalCompileTimeMs += compileTimeMs;
        if (tier == Tier::Baseline) stats_.baselineCompilations++;
        else if (tier == Tier::Optimized) stats_.optimizedCompilations++;
    }

    // Check if a function is suitable for inlining.
    bool shouldInline(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = profiles_.find(functionId);
        if (it == profiles_.end()) return false;
        auto& p = it->second;
        return p.bytecodeSize <= config_.maxBytecodeSizeForInline
            && p.callCount >= 10
            && !p.blacklisted;
    }

    void setBytecodeSize(uint32_t functionId, uint32_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        profiles_[functionId].bytecodeSize = size;
    }

    Tier currentTier(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = profiles_.find(functionId);
        return it != profiles_.end() ? it->second.currentTier : Tier::Interpreter;
    }

    struct Stats {
        uint64_t totalCompilations = 0;
        uint64_t baselineCompilations = 0;
        uint64_t optimizedCompilations = 0;
        double totalCompileTimeMs = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    TierConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, FunctionProfile> profiles_;
    Stats stats_;
};

} // namespace Zepra::JIT
