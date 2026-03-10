// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_phase_timer.cpp — Hierarchical phase timing for GC sub-phases

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <chrono>
#include <vector>
#include <string>
#include <mutex>

namespace Zepra::Heap {

enum class GCPhase : uint8_t {
    Begin,
    RootScan,
    Mark,
    MarkRoots,
    MarkTransitive,
    MarkWeakRefs,
    MarkEphemerons,
    Sweep,
    SweepArenas,
    SweepLargeObjects,
    SweepAtoms,
    Compact,
    CompactSelect,
    CompactMove,
    CompactUpdateRefs,
    Finalize,
    FinalizeDestructors,
    FinalizeWeakCaches,
    Decommit,
    End,
    Count,
};

static const char* phaseName(GCPhase phase) {
    static const char* names[] = {
        "Begin", "RootScan", "Mark", "MarkRoots", "MarkTransitive",
        "MarkWeakRefs", "MarkEphemerons", "Sweep", "SweepArenas",
        "SweepLargeObjects", "SweepAtoms", "Compact", "CompactSelect",
        "CompactMove", "CompactUpdateRefs", "Finalize", "FinalizeDestructors",
        "FinalizeWeakCaches", "Decommit", "End"
    };
    size_t idx = static_cast<size_t>(phase);
    return idx < static_cast<size_t>(GCPhase::Count) ? names[idx] : "Unknown";
}

struct PhaseRecord {
    GCPhase phase;
    double durationMs;
    uint64_t cycleId;
};

class GCPhaseTimer {
public:
    GCPhaseTimer() : currentPhase_(GCPhase::Count), cycleId_(0) {}

    void beginCycle(uint64_t cycleId) {
        cycleId_ = cycleId;
        // Reset per-cycle durations.
        for (int i = 0; i < static_cast<int>(GCPhase::Count); i++) {
            cycleDurations_[i] = 0;
        }
    }

    void enterPhase(GCPhase phase) {
        if (currentPhase_ != GCPhase::Count) {
            exitPhase();
        }
        currentPhase_ = phase;
        phaseStart_ = std::chrono::steady_clock::now();
    }

    double exitPhase() {
        if (currentPhase_ == GCPhase::Count) return 0;

        auto end = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - phaseStart_).count();

        int idx = static_cast<int>(currentPhase_);
        cycleDurations_[idx] += ms;

        PhaseRecord record;
        record.phase = currentPhase_;
        record.durationMs = ms;
        record.cycleId = cycleId_;
        history_.push_back(record);

        // Accumulate totals.
        totalDurations_[idx] += ms;
        phaseCounts_[idx]++;

        currentPhase_ = GCPhase::Count;
        return ms;
    }

    double phaseDuration(GCPhase phase) const {
        return cycleDurations_[static_cast<int>(phase)];
    }

    double totalPhaseDuration(GCPhase phase) const {
        return totalDurations_[static_cast<int>(phase)];
    }

    uint64_t phaseCount(GCPhase phase) const {
        return phaseCounts_[static_cast<int>(phase)];
    }

    double averagePhaseDuration(GCPhase phase) const {
        int idx = static_cast<int>(phase);
        return phaseCounts_[idx] > 0 ? totalDurations_[idx] / phaseCounts_[idx] : 0;
    }

    double cycleTotalMs() const {
        double total = 0;
        for (int i = 0; i < static_cast<int>(GCPhase::Count); i++) {
            total += cycleDurations_[i];
        }
        return total;
    }

    // Find the most expensive phase in the current cycle.
    GCPhase hottestPhase() const {
        int maxIdx = 0;
        for (int i = 1; i < static_cast<int>(GCPhase::Count); i++) {
            if (cycleDurations_[i] > cycleDurations_[maxIdx]) maxIdx = i;
        }
        return static_cast<GCPhase>(maxIdx);
    }

    // Phase breakdown as percentage of cycle total.
    double phasePercentage(GCPhase phase) const {
        double total = cycleTotalMs();
        return total > 0 ? (phaseDuration(phase) / total) * 100.0 : 0;
    }

    GCPhase currentPhase() const { return currentPhase_; }
    bool isInPhase() const { return currentPhase_ != GCPhase::Count; }

    const std::vector<PhaseRecord>& history() const { return history_; }

    void clearHistory() { history_.clear(); }

private:
    GCPhase currentPhase_;
    uint64_t cycleId_;
    std::chrono::steady_clock::time_point phaseStart_;
    double cycleDurations_[static_cast<int>(GCPhase::Count)] = {};
    double totalDurations_[static_cast<int>(GCPhase::Count)] = {};
    uint64_t phaseCounts_[static_cast<int>(GCPhase::Count)] = {};
    std::vector<PhaseRecord> history_;
};

} // namespace Zepra::Heap
