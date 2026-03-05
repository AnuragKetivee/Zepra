// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_bg_throttle.cpp — Background task throttle for minimal CPU

#include <atomic>
#include <mutex>
#include <functional>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <chrono>

namespace Zepra::Heap {

// ZepraBrowser minimizes background CPU. GC background tasks
// (concurrent marking, sweeping) are throttled when the tab is
// not visible, reducing battery and CPU usage.

enum class TabVisibility : uint8_t {
    Active,       // Full GC throughput
    Visible,      // Normal throughput
    Hidden,       // Throttled — 25% duty cycle
    Frozen        // No background GC work
};

class BackgroundThrottle {
public:
    struct Config {
        double activeMarkerThreads;
        double hiddenDutyCycle;       // Fraction of time GC can run when hidden
        uint64_t frozenGracePeriodMs; // Allow one GC before frozen

        Config()
            : activeMarkerThreads(2)
            , hiddenDutyCycle(0.25)
            , frozenGracePeriodMs(30000) {}
    };

    explicit BackgroundThrottle(const Config& config = Config{})
        : config_(config), visibility_(TabVisibility::Active) {}

    void setVisibility(TabVisibility v) {
        visibility_ = v;
        if (v == TabVisibility::Frozen) {
            frozenAt_ = std::chrono::steady_clock::now();
        }
    }

    TabVisibility visibility() const { return visibility_; }

    // Should GC background work proceed right now?
    bool shouldRun() const {
        switch (visibility_) {
            case TabVisibility::Active:
            case TabVisibility::Visible:
                return true;
            case TabVisibility::Hidden:
                return isWithinDutyCycle();
            case TabVisibility::Frozen:
                return isInGracePeriod();
        }
        return false;
    }

    // How many marker threads should be active?
    size_t markerThreadCount() const {
        switch (visibility_) {
            case TabVisibility::Active:
                return static_cast<size_t>(config_.activeMarkerThreads);
            case TabVisibility::Visible:
                return static_cast<size_t>(config_.activeMarkerThreads);
            case TabVisibility::Hidden:
                return 1;
            case TabVisibility::Frozen:
                return 0;
        }
        return 0;
    }

    // How long to sleep between GC work chunks when throttled.
    uint64_t sleepMs() const {
        switch (visibility_) {
            case TabVisibility::Active: return 0;
            case TabVisibility::Visible: return 0;
            case TabVisibility::Hidden: return 30;
            case TabVisibility::Frozen: return 1000;
        }
        return 0;
    }

private:
    bool isWithinDutyCycle() const {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        // Simple duty cycle: run for (ratio * 100)ms out of every 100ms.
        uint64_t period = 100;
        uint64_t onTime = static_cast<uint64_t>(period * config_.hiddenDutyCycle);
        return (static_cast<uint64_t>(ms) % period) < onTime;
    }

    bool isInGracePeriod() const {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(
            now - frozenAt_).count();
        return elapsed < config_.frozenGracePeriodMs;
    }

    Config config_;
    TabVisibility visibility_;
    std::chrono::steady_clock::time_point frozenAt_;
};

} // namespace Zepra::Heap
