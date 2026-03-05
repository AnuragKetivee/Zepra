/**
 * @file gc_integration.cpp
 * @brief VM ↔ GC bridge — connects engine runtime to heap subsystem
 *
 * This file wires the GC into the JS engine:
 * - Root enumeration (stack frames, globals, handles, compiled code)
 * - Object tracing (shape-based field enumeration)
 * - Write barrier dispatch
 * - Safe-point synchronization
 * - GC event callbacks to embedder
 * - External memory tracking (ArrayBuffer, WASM memory)
 */

#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cassert>
#include <memory>
#include <algorithm>

namespace Zepra::Heap {

// =============================================================================
// Root Category
// =============================================================================

enum class RootCategory : uint8_t {
    Stack,              // Stack frame references
    Global,             // Global object properties
    HandleScope,        // GC handles from native code
    WeakHandles,        // Weak persistent handles
    CompiledCode,       // JIT code references
    BuiltinObjects,     // Built-in prototypes (Array, Object, etc.)
    RegExp,             // Compiled regex
    DebugInfo,          // Debugger-related roots
    ExternalStrings,    // Externally-owned strings
    Embedder,           // Embedder-provided roots
};

static const char* rootCategoryName(RootCategory c) {
    switch (c) {
        case RootCategory::Stack: return "Stack";
        case RootCategory::Global: return "Global";
        case RootCategory::HandleScope: return "HandleScope";
        case RootCategory::WeakHandles: return "WeakHandles";
        case RootCategory::CompiledCode: return "CompiledCode";
        case RootCategory::BuiltinObjects: return "BuiltinObjects";
        case RootCategory::RegExp: return "RegExp";
        case RootCategory::DebugInfo: return "DebugInfo";
        case RootCategory::ExternalStrings: return "ExternalStrings";
        case RootCategory::Embedder: return "Embedder";
        default: return "Unknown";
    }
}

// =============================================================================
// Root Enumerator
// =============================================================================

class RootEnumerator {
public:
    using SlotVisitor = std::function<void(void** slot)>;
    using RootProvider = std::function<void(SlotVisitor)>;

    void registerProvider(RootCategory category, RootProvider provider) {
        std::lock_guard<std::mutex> lock(mutex_);
        providers_.push_back({category, std::move(provider)});
    }

    void enumerateAll(SlotVisitor visitor) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : providers_) {
            currentCategory_ = entry.category;
            entry.provider(visitor);
        }
    }

    void enumerateCategory(RootCategory category, SlotVisitor visitor) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : providers_) {
            if (entry.category == category) {
                entry.provider(visitor);
            }
        }
    }

    size_t providerCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return providers_.size();
    }

    RootCategory currentCategory() const { return currentCategory_; }

private:
    struct Entry {
        RootCategory category;
        RootProvider provider;
    };
    std::vector<Entry> providers_;
    RootCategory currentCategory_ = RootCategory::Stack;
    mutable std::mutex mutex_;
};

// =============================================================================
// External Memory Tracker
// =============================================================================

class ExternalMemoryTracker {
public:
    void reportAllocation(size_t bytes) {
        externalBytes_.fetch_add(bytes, std::memory_order_relaxed);
        totalAllocated_.fetch_add(bytes, std::memory_order_relaxed);
        allocCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void reportDeallocation(size_t bytes) {
        size_t current = externalBytes_.load(std::memory_order_relaxed);
        size_t toSub = std::min(current, bytes);
        externalBytes_.fetch_sub(toSub, std::memory_order_relaxed);
        totalFreed_.fetch_add(bytes, std::memory_order_relaxed);
        freeCount_.fetch_add(1, std::memory_order_relaxed);
    }

    size_t currentBytes() const {
        return externalBytes_.load(std::memory_order_relaxed);
    }

    size_t totalAllocated() const {
        return totalAllocated_.load(std::memory_order_relaxed);
    }

    size_t totalFreed() const {
        return totalFreed_.load(std::memory_order_relaxed);
    }

    bool exceedsThreshold(size_t threshold) const {
        return currentBytes() > threshold;
    }

private:
    std::atomic<size_t> externalBytes_{0};
    std::atomic<size_t> totalAllocated_{0};
    std::atomic<size_t> totalFreed_{0};
    std::atomic<size_t> allocCount_{0};
    std::atomic<size_t> freeCount_{0};
};

// =============================================================================
// GC Event Dispatcher
// =============================================================================

class GCEventDispatcher {
public:
    enum class EventType : uint8_t {
        GCStart,
        GCEnd,
        MarkStart,
        MarkEnd,
        SweepStart,
        SweepEnd,
        CompactStart,
        CompactEnd,
        AllocationFailure,
        HeapGrow,
        HeapShrink,
        OOMWarning,
    };

    struct GCEvent {
        EventType type;
        uint64_t timestampUs;
        size_t heapUsed;
        size_t heapCapacity;
        double pauseMs;
        size_t bytesReclaimed;
    };

    using EventCallback = std::function<void(const GCEvent&)>;

    void addEventListener(EventCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners_.push_back(std::move(callback));
    }

    void dispatch(const GCEvent& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& listener : listeners_) {
            listener(event);
        }
    }

    void dispatchSimple(EventType type) {
        GCEvent event;
        event.type = type;
        event.timestampUs = nowUs();
        event.heapUsed = 0;
        event.heapCapacity = 0;
        event.pauseMs = 0;
        event.bytesReclaimed = 0;
        dispatch(event);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners_.clear();
    }

private:
    static uint64_t nowUs() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::vector<EventCallback> listeners_;
    std::mutex mutex_;
};

// =============================================================================
// GC Integration Bridge
// =============================================================================

class GCIntegration {
public:
    struct Config {
        size_t externalMemoryThreshold;
        bool enableVerification;
        bool verboseGCLogging;

        Config()
            : externalMemoryThreshold(64 * 1024 * 1024)
            , enableVerification(false)
            , verboseGCLogging(false) {}
    };

    explicit GCIntegration(const Config& config = Config{})
        : config_(config) {}

    // Root enumeration
    RootEnumerator& rootEnumerator() { return rootEnumerator_; }

    // External memory
    ExternalMemoryTracker& externalMemory() { return externalMemory_; }

    // Events
    GCEventDispatcher& eventDispatcher() { return eventDispatcher_; }

    /**
     * @brief Check if external memory should trigger GC
     */
    bool shouldGCForExternalMemory() const {
        return externalMemory_.exceedsThreshold(config_.externalMemoryThreshold);
    }

    /**
     * @brief Log GC event (if verbose)
     */
    void logGCEvent(const char* format, ...) {
        if (!config_.verboseGCLogging) return;
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
    }

    /**
     * @brief Total memory (heap + external)
     */
    size_t totalManagedMemory(size_t heapUsed) const {
        return heapUsed + externalMemory_.currentBytes();
    }

private:
    Config config_;
    RootEnumerator rootEnumerator_;
    ExternalMemoryTracker externalMemory_;
    GCEventDispatcher eventDispatcher_;
};

} // namespace Zepra::Heap
