// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file jit_gc_bridge.cpp
 * @brief JIT ↔ GC coordination layer
 *
 * Connects the JIT compiler with the GC:
 *
 * 1. IC (Inline Cache) Invalidation
 *    When GC moves objects or changes shapes, inline caches that
 *    cached old addresses or shape pointers become stale.
 *    The GC notifies the JIT to patch or invalidate affected ICs.
 *
 * 2. Embedded Reference Tracking
 *    JIT code embeds raw pointers to heap objects (constants,
 *    prototype chains, type feedback). The GC must know about
 *    these so it can:
 *    a) Keep the referenced objects alive (treat as roots)
 *    b) Update pointers when objects move (compaction)
 *
 * 3. Code Liveness
 *    JIT code itself lives in the heap (code space). Dead code
 *    must be collected. But code on the stack must not be collected.
 *    The GC must check all stack frames before sweeping code.
 *
 * 4. Safe-point Coordination
 *    JIT code inserts safe-point polls at back-edges and function
 *    prologues. These check the GC's safe-point flag and park
 *    the thread if a GC is requested.
 *
 * 5. OSR (On-Stack Replacement)
 *    When optimized code is invalidated during GC, the engine
 *    must deopt back to baseline. This requires patching the
 *    return address on active stack frames.
 */

#include <atomic>
#include <mutex>
#include <shared_mutex>
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
// Compiled Code Descriptor
// =============================================================================

/**
 * @brief Metadata for a single compiled function
 *
 * Every JIT-compiled function has one of these.
 * Stored outside the code itself so it survives code GC.
 */
struct CompiledCodeDescriptor {
    uint32_t functionId;
    uintptr_t codeStart;       // Start of machine code
    size_t codeSize;           // Size of machine code buffer
    uintptr_t codeEnd() const { return codeStart + codeSize; }

    // Tier info
    enum class Tier : uint8_t {
        Baseline = 0,
        DFG = 1,
        FTL = 2,
    };
    Tier tier;

    // Embedded heap references (GC roots in code)
    struct EmbeddedRef {
        size_t offsetInCode;   // Offset from codeStart
        void* heapObject;     // The referenced object
        enum class Kind : uint8_t {
            Constant,          // Literal object reference
            Prototype,         // Prototype chain pointer
            Structure,         // Shape/hidden class pointer
            FeedbackVector,    // Type feedback data
            GlobalCell,        // Global variable cell
        } kind;
    };
    std::vector<EmbeddedRef> embeddedRefs;

    // Inline caches
    struct ICDescriptor {
        size_t offsetInCode;   // Offset of IC site in code
        uint32_t icId;         // IC identifier
        enum class Type : uint8_t {
            GetProperty,
            SetProperty,
            Call,
            Instanceof,
            TypeOf,
        } type;
        void* cachedStructure; // Cached shape pointer (may become stale)
        void* cachedSlot;      // Cached property slot
    };
    std::vector<ICDescriptor> inlineCaches;

    // Safe-point descriptors (where GC can interrupt)
    struct SafePoint {
        size_t offsetInCode;
        uint16_t stackMapSize;
        // Offsets of live reference slots from frame base
        std::vector<int16_t> liveRefOffsets;
    };
    std::vector<SafePoint> safePoints;

    // State
    bool onStack;              // Currently on any thread's stack
    bool markedAlive;          // Marked alive during current GC
    bool invalidated;          // Deoptimized — should not be entered
    uint64_t lastUsedUs;       // For aging/eviction

    CompiledCodeDescriptor()
        : functionId(0), codeStart(0), codeSize(0)
        , tier(Tier::Baseline), onStack(false)
        , markedAlive(false), invalidated(false), lastUsedUs(0) {}

    bool containsPC(uintptr_t pc) const {
        return pc >= codeStart && pc < codeEnd();
    }
};

// =============================================================================
// IC Invalidation Manager
// =============================================================================

/**
 * @brief Tracks and invalidates inline caches affected by GC
 *
 * When GC moves an object or transitions a shape, ICs that cached
 * the old address must be patched to miss (forcing re-lookup).
 */
class ICInvalidationManager {
public:
    struct Stats {
        uint64_t totalInvalidations;
        uint64_t structureTransitions;
        uint64_t objectMoves;
        uint64_t icSitesPatched;
    };

    /**
     * @brief Register an IC site with its cached structure
     */
    void registerIC(uint32_t icId, uintptr_t codeAddr,
                    void* cachedStructure) {
        std::lock_guard<std::mutex> lock(mutex_);
        ICEntry entry;
        entry.icId = icId;
        entry.codeAddr = codeAddr;
        entry.cachedStructure = cachedStructure;
        entries_[icId] = entry;

        // Index by structure for fast invalidation
        structureIndex_[reinterpret_cast<uintptr_t>(cachedStructure)]
            .push_back(icId);
    }

    void unregisterIC(uint32_t icId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(icId);
        if (it != entries_.end()) {
            removeFromStructureIndex(icId, it->second.cachedStructure);
            entries_.erase(it);
        }
    }

    /**
     * @brief Invalidate all ICs that cache a given structure/shape
     *
     * Called when a shape transition occurs (e.g. adding a property).
     * @param patchCallback Called for each IC site to patch the code
     */
    size_t invalidateByStructure(
        void* structure,
        std::function<void(uintptr_t codeAddr, uint32_t icId)> patchCallback
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto key = reinterpret_cast<uintptr_t>(structure);
        auto it = structureIndex_.find(key);
        if (it == structureIndex_.end()) return 0;

        size_t count = 0;
        for (uint32_t icId : it->second) {
            auto entryIt = entries_.find(icId);
            if (entryIt != entries_.end()) {
                if (patchCallback) {
                    patchCallback(entryIt->second.codeAddr, icId);
                }
                entryIt->second.cachedStructure = nullptr;
                count++;
            }
        }

        structureIndex_.erase(it);
        stats_.icSitesPatched += count;
        stats_.structureTransitions++;
        return count;
    }

    /**
     * @brief Update IC entries when objects move (compaction)
     *
     * @param forwarding Maps old addresses → new addresses
     */
    size_t updateForMovedObjects(
        const std::unordered_map<uintptr_t, uintptr_t>& forwarding,
        std::function<void(uintptr_t codeAddr, uintptr_t oldRef,
                           uintptr_t newRef)> patchCallback
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t updated = 0;
        for (auto& [id, entry] : entries_) {
            if (!entry.cachedStructure) continue;

            auto oldAddr = reinterpret_cast<uintptr_t>(entry.cachedStructure);
            auto it = forwarding.find(oldAddr);
            if (it != forwarding.end()) {
                if (patchCallback) {
                    patchCallback(entry.codeAddr, oldAddr, it->second);
                }
                entry.cachedStructure = reinterpret_cast<void*>(it->second);
                updated++;
            }
        }

        stats_.objectMoves++;
        return updated;
    }

    /**
     * @brief Invalidate ALL inline caches (e.g. after major GC)
     */
    size_t invalidateAll(
        std::function<void(uintptr_t codeAddr, uint32_t icId)> patchCallback
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t count = 0;
        for (auto& [id, entry] : entries_) {
            if (entry.cachedStructure) {
                if (patchCallback) {
                    patchCallback(entry.codeAddr, id);
                }
                entry.cachedStructure = nullptr;
                count++;
            }
        }

        structureIndex_.clear();
        stats_.totalInvalidations++;
        stats_.icSitesPatched += count;
        return count;
    }

    Stats stats() const { return stats_; }

private:
    struct ICEntry {
        uint32_t icId;
        uintptr_t codeAddr;
        void* cachedStructure;
    };

    void removeFromStructureIndex(uint32_t icId, void* structure) {
        if (!structure) return;
        auto key = reinterpret_cast<uintptr_t>(structure);
        auto it = structureIndex_.find(key);
        if (it != structureIndex_.end()) {
            auto& vec = it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), icId), vec.end());
            if (vec.empty()) structureIndex_.erase(it);
        }
    }

    std::mutex mutex_;
    std::unordered_map<uint32_t, ICEntry> entries_;
    std::unordered_map<uintptr_t, std::vector<uint32_t>> structureIndex_;
    Stats stats_{};
};

// =============================================================================
// Code Root Set
// =============================================================================

/**
 * @brief Tracks all heap references embedded in JIT code
 *
 * The GC treats these as roots: objects referenced from JIT code
 * must not be collected. After compaction, these pointers must
 * be patched in the machine code.
 */
class CodeRootSet {
public:
    struct Entry {
        uintptr_t patchLocation;   // Address in JIT code to patch
        void* heapObject;          // The referenced heap object
        uint32_t functionId;
    };

    void addRoot(uintptr_t patchLoc, void* obj, uint32_t funcId) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back({patchLoc, obj, funcId});

        objectIndex_[reinterpret_cast<uintptr_t>(obj)]
            .push_back(entries_.size() - 1);
    }

    void removeRootsForFunction(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [functionId](const Entry& e) {
                    return e.functionId == functionId;
                }),
            entries_.end());
        rebuildIndex();
    }

    /**
     * @brief Enumerate all roots (called by GC during root scanning)
     */
    void enumerateRoots(std::function<void(void** slot)> visitor) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : entries_) {
            visitor(&entry.heapObject);
        }
    }

    /**
     * @brief Update embedded pointers after compaction
     *
     * For each root whose heap object moved, patch the machine
     * code at patchLocation with the new address.
     */
    size_t updateAfterCompaction(
        const std::unordered_map<uintptr_t, uintptr_t>& forwarding,
        std::function<void(uintptr_t patchLoc, uintptr_t newAddr)> codePatcher
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t patched = 0;

        for (auto& entry : entries_) {
            auto oldAddr = reinterpret_cast<uintptr_t>(entry.heapObject);
            auto it = forwarding.find(oldAddr);
            if (it != forwarding.end()) {
                entry.heapObject = reinterpret_cast<void*>(it->second);
                if (codePatcher) {
                    codePatcher(entry.patchLocation, it->second);
                }
                patched++;
            }
        }

        rebuildIndex();
        return patched;
    }

    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

private:
    void rebuildIndex() {
        objectIndex_.clear();
        for (size_t i = 0; i < entries_.size(); i++) {
            objectIndex_[reinterpret_cast<uintptr_t>(entries_[i].heapObject)]
                .push_back(i);
        }
    }

    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    std::unordered_map<uintptr_t, std::vector<size_t>> objectIndex_;
};

// =============================================================================
// JIT Code Registry
// =============================================================================

/**
 * @brief Registry of all JIT-compiled code in the heap
 *
 * Maps program counters to function descriptors.
 * Used by:
 * - Stack walker (find GC map for a PC)
 * - Code GC (find and collect dead code)
 * - Deoptimizer (find code to invalidate)
 */
class JITCodeRegistry {
public:
    void registerCode(CompiledCodeDescriptor desc) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t funcId = desc.functionId;
        codeMap_[funcId] = std::move(desc);
    }

    void unregisterCode(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        codeMap_.erase(functionId);
    }

    CompiledCodeDescriptor* findByPC(uintptr_t pc) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, desc] : codeMap_) {
            if (desc.containsPC(pc)) return &desc;
        }
        return nullptr;
    }

    CompiledCodeDescriptor* findByFunctionId(uint32_t functionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = codeMap_.find(functionId);
        return it != codeMap_.end() ? &it->second : nullptr;
    }

    /**
     * @brief Mark code as on-stack (prevents code GC)
     *
     * Called during stack walking to identify live code.
     */
    void markOnStack(uintptr_t pc) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, desc] : codeMap_) {
            if (desc.containsPC(pc)) {
                desc.onStack = true;
                desc.markedAlive = true;
                break;
            }
        }
    }

    /**
     * @brief Collect dead code (not on stack, not marked alive)
     *
     * @param releaseCallback Called to free the code memory
     * @return Number of code blocks collected
     */
    size_t collectDeadCode(
        std::function<void(uintptr_t codeStart, size_t codeSize)> releaseCallback
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<uint32_t> dead;

        for (auto& [id, desc] : codeMap_) {
            if (!desc.markedAlive && !desc.onStack) {
                dead.push_back(id);
                if (releaseCallback) {
                    releaseCallback(desc.codeStart, desc.codeSize);
                }
            }
        }

        for (uint32_t id : dead) {
            codeMap_.erase(id);
        }

        return dead.size();
    }

    /**
     * @brief Reset marks for next GC cycle
     */
    void resetMarks() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, desc] : codeMap_) {
            desc.onStack = false;
            desc.markedAlive = false;
        }
    }

    /**
     * @brief Iterate all registered code
     */
    void forEach(
        std::function<void(const CompiledCodeDescriptor&)> visitor
    ) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, desc] : codeMap_) {
            visitor(desc);
        }
    }

    size_t codeCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return codeMap_.size();
    }

    size_t totalCodeBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (const auto& [id, desc] : codeMap_) {
            total += desc.codeSize;
        }
        return total;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, CompiledCodeDescriptor> codeMap_;
};

// =============================================================================
// JIT-GC Bridge
// =============================================================================

/**
 * @brief Top-level coordinator between JIT and GC
 */
class JITGCBridge {
public:
    JITGCBridge() = default;

    ICInvalidationManager& icManager() { return icManager_; }
    CodeRootSet& codeRoots() { return codeRoots_; }
    JITCodeRegistry& codeRegistry() { return codeRegistry_; }

    /**
     * @brief Pre-GC: prepare JIT state for collection
     */
    void prepareForGC() {
        codeRegistry_.resetMarks();
    }

    /**
     * @brief During GC: scan code roots
     */
    void scanCodeRoots(std::function<void(void** slot)> visitor) {
        codeRoots_.enumerateRoots(visitor);
    }

    /**
     * @brief During stack walk: mark code as live
     */
    void markCodeOnStack(uintptr_t pc) {
        codeRegistry_.markOnStack(pc);
    }

    /**
     * @brief Post-GC: collect dead code and update references
     */
    struct PostGCResult {
        size_t codeBlocksCollected;
        size_t embeddedRefsPatched;
        size_t icSitesInvalidated;
    };

    PostGCResult postGC(
        const std::unordered_map<uintptr_t, uintptr_t>& forwarding,
        std::function<void(uintptr_t codeAddr, uintptr_t newAddr)> codePatcher,
        std::function<void(uintptr_t codeStart, size_t codeSize)> codeReleaser
    ) {
        PostGCResult result{};

        // Collect dead code
        result.codeBlocksCollected = codeRegistry_.collectDeadCode(codeReleaser);

        // Patch embedded references in surviving code
        if (!forwarding.empty()) {
            result.embeddedRefsPatched =
                codeRoots_.updateAfterCompaction(forwarding, codePatcher);
        }

        return result;
    }

private:
    ICInvalidationManager icManager_;
    CodeRootSet codeRoots_;
    JITCodeRegistry codeRegistry_;
};

} // namespace Zepra::Heap
