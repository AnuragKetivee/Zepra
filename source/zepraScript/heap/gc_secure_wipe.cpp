// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_secure_wipe.cpp — Zero freed memory to prevent data leaks

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>

namespace Zepra::Heap {

// ZepraBrowser promise: no user data tracking, no data selling.
// Freed heap memory is zeroed before reuse to ensure:
// - No cross-site data leaks via recycled allocations
// - No sensitive info (passwords, tokens) left in freed pages
// - No speculative execution side-channel on stale heap data

class SecureWipe {
public:
    // Wipe a freed object before returning to free list.
    static void wipeObject(uintptr_t addr, size_t bytes) {
        volatile uint8_t* ptr = reinterpret_cast<volatile uint8_t*>(addr);
        for (size_t i = 0; i < bytes; i++) {
            ptr[i] = 0;
        }
    }

    // Optimized wipe for aligned, large blocks (pages).
    static void wipePage(uintptr_t addr, size_t bytes) {
        volatile uint64_t* ptr = reinterpret_cast<volatile uint64_t*>(addr);
        size_t qwords = bytes / 8;
        for (size_t i = 0; i < qwords; i++) {
            ptr[i] = 0;
        }
        // Handle trailing bytes.
        size_t remainder = bytes % 8;
        if (remainder > 0) {
            volatile uint8_t* tail = reinterpret_cast<volatile uint8_t*>(
                addr + qwords * 8);
            for (size_t i = 0; i < remainder; i++) {
                tail[i] = 0;
            }
        }
    }

    // Verify a region is zeroed (debug builds).
    static bool verifyZeroed(uintptr_t addr, size_t bytes) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(addr);
        for (size_t i = 0; i < bytes; i++) {
            if (ptr[i] != 0) return false;
        }
        return true;
    }

    // Wipe-on-free policy.
    enum class Policy : uint8_t {
        Never,           // No wiping (test/development only)
        SensitiveOnly,   // Only wipe objects marked sensitive (strings, buffers)
        Always           // Wipe everything (browser default)
    };

    void setPolicy(Policy p) { policy_ = p; }
    Policy policy() const { return policy_; }

    void wipeIfNeeded(uintptr_t addr, size_t bytes, bool isSensitive) {
        switch (policy_) {
            case Policy::Never: return;
            case Policy::SensitiveOnly:
                if (!isSensitive) return;
                break;
            case Policy::Always:
                break;
        }
        wipeObject(addr, bytes);
        stats_.bytesWiped += bytes;
        stats_.objectsWiped++;
    }

    struct Stats {
        uint64_t bytesWiped;
        uint64_t objectsWiped;
    };

    const Stats& stats() const { return stats_; }

private:
    Policy policy_ = Policy::Always;
    Stats stats_{};
};

} // namespace Zepra::Heap
