// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_slice_budget.cpp — Time-sliced incremental work budget

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <chrono>
#include <algorithm>

namespace Zepra::Heap {

class SliceBudget {
public:
    enum class Mode : uint8_t {
        TimeBased,       // Budget in milliseconds
        WorkBased,       // Budget in work units (e.g., cells marked)
        Unlimited,       // No limit (full atomic GC)
    };

    // Create a time-based budget.
    static SliceBudget timeBudget(double ms) {
        SliceBudget b;
        b.mode_ = Mode::TimeBased;
        b.budgetMs_ = ms;
        b.deadline_ = std::chrono::steady_clock::now() +
                      std::chrono::microseconds(static_cast<int64_t>(ms * 1000));
        return b;
    }

    // Create a work-based budget.
    static SliceBudget workBudget(size_t units) {
        SliceBudget b;
        b.mode_ = Mode::WorkBased;
        b.workBudget_ = units;
        b.workDone_ = 0;
        return b;
    }

    static SliceBudget unlimited() {
        SliceBudget b;
        b.mode_ = Mode::Unlimited;
        return b;
    }

    SliceBudget() : mode_(Mode::Unlimited), budgetMs_(0), workBudget_(0), workDone_(0)
        , overrunMs_(0), checkInterval_(1000) {}

    // Check if budget is exhausted. For time-based, only checks every N work units
    // to avoid excessive clock reads.
    bool isOverBudget() {
        switch (mode_) {
            case Mode::TimeBased:
                checkCount_++;
                if (checkCount_ % checkInterval_ != 0) return false;
                return std::chrono::steady_clock::now() >= deadline_;

            case Mode::WorkBased:
                return workDone_ >= workBudget_;

            case Mode::Unlimited:
                return false;
        }
        return false;
    }

    void recordWork(size_t units = 1) {
        workDone_ += units;
    }

    // Finalize: compute overrun if time-based.
    void finalize() {
        if (mode_ == Mode::TimeBased) {
            auto now = std::chrono::steady_clock::now();
            if (now > deadline_) {
                overrunMs_ = std::chrono::duration<double, std::milli>(now - deadline_).count();
            }
            elapsed_ = std::chrono::duration<double, std::milli>(
                now - (deadline_ - std::chrono::microseconds(
                    static_cast<int64_t>(budgetMs_ * 1000)))).count();
        }
    }

    Mode mode() const { return mode_; }
    double budgetMs() const { return budgetMs_; }
    size_t workBudget() const { return workBudget_; }
    size_t workDone() const { return workDone_; }
    double overrunMs() const { return overrunMs_; }
    double elapsedMs() const { return elapsed_; }
    bool isUnlimited() const { return mode_ == Mode::Unlimited; }

    // Remaining budget.
    double remainingMs() const {
        if (mode_ != Mode::TimeBased) return 0;
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline_) return 0;
        return std::chrono::duration<double, std::milli>(deadline_ - now).count();
    }

    size_t remainingWork() const {
        if (mode_ != Mode::WorkBased) return 0;
        return workDone_ < workBudget_ ? workBudget_ - workDone_ : 0;
    }

    // Adjust check interval based on observed cost per check.
    void setCheckInterval(size_t interval) {
        checkInterval_ = std::max<size_t>(1, interval);
    }

private:
    Mode mode_;
    double budgetMs_;
    size_t workBudget_;
    size_t workDone_;
    double overrunMs_;
    double elapsed_ = 0;
    std::chrono::steady_clock::time_point deadline_;
    size_t checkInterval_;
    size_t checkCount_ = 0;
};

} // namespace Zepra::Heap
