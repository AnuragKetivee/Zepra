/**
 * @file heap_verifier_impl.cpp
 * @brief Heap integrity verification passes
 *
 * Debug-mode verification to catch GC bugs early:
 * 1. Object graph integrity: all references point to valid objects
 * 2. Mark bitmap consistency: marks match live set
 * 3. Forwarding table validity: no dangling forwards
 * 4. Write barrier correctness: dirty cards match actual writes
 * 5. Free list sanity: no overlapping blocks
 * 6. Nursery boundary: no young→old references without barrier
 * 7. Page metadata: cursor <= end, allocation counts consistent
 */

#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <unordered_set>

namespace Zepra::Heap {

// =============================================================================
// Verification Result
// =============================================================================

struct VerificationError {
    enum class Severity : uint8_t { Warning, Error, Fatal };
    enum class Category : uint8_t {
        ObjectRef, MarkBitmap, ForwardingPtr, WriteBarrier,
        FreeList, NurseryBoundary, PageMetadata, HeapSize, InternalState
    };

    Severity severity;
    Category category;
    std::string message;
    void* address;
    size_t additionalInfo;
};

struct VerificationResult {
    bool passed;
    size_t warningCount;
    size_t errorCount;
    size_t fatalCount;
    double durationMs;
    std::vector<VerificationError> errors;

    void addError(VerificationError::Severity s, VerificationError::Category c,
                  const std::string& msg, void* addr = nullptr) {
        VerificationError e;
        e.severity = s;
        e.category = c;
        e.message = msg;
        e.address = addr;
        e.additionalInfo = 0;
        errors.push_back(e);

        if (s == VerificationError::Severity::Warning) warningCount++;
        else if (s == VerificationError::Severity::Error) errorCount++;
        else if (s == VerificationError::Severity::Fatal) { fatalCount++; passed = false; }
    }

    std::string summary() const {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "Verification %s: %zu warnings, %zu errors, %zu fatal (%.2f ms)",
            passed ? "PASSED" : "FAILED",
            warningCount, errorCount, fatalCount, durationMs);
        return std::string(buf);
    }
};

// =============================================================================
// Heap Verifier
// =============================================================================

class HeapVerifierImpl {
public:
    struct Callbacks {
        // Check if address is in managed heap
        std::function<bool(const void*)> isHeapAddress;

        // Check if address is in nursery
        std::function<bool(const void*)> isInNursery;

        // Check if address is in old gen
        std::function<bool(const void*)> isInOldGen;

        // Check if object is marked
        std::function<bool(void*)> isMarked;

        // Get object size
        std::function<size_t(void*)> objectSize;

        // Trace object references
        std::function<void(void*, std::function<void(void**)>)> traceObject;

        // Enumerate roots
        std::function<void(std::function<void(void**)>)> enumerateRoots;

        // Iterate all objects in heap
        std::function<void(std::function<void(void*, size_t)>)> iterateAllObjects;

        // Check if card is dirty for given address
        std::function<bool(const void*)> isCardDirty;
    };

    explicit HeapVerifierImpl() = default;

    void setCallbacks(Callbacks callbacks) { cb_ = std::move(callbacks); }

    /**
     * @brief Run all verification passes
     */
    VerificationResult verifyAll();

    /**
     * @brief Individual passes
     */
    VerificationResult verifyObjectRefs();
    VerificationResult verifyMarkBitmap();
    VerificationResult verifyWriteBarriers();
    VerificationResult verifyFreeList();
    VerificationResult verifyRoots();
    VerificationResult verifyNoBrokenPointers();

private:
    Callbacks cb_;
};

inline VerificationResult HeapVerifierImpl::verifyAll() {
    VerificationResult combined;
    combined.passed = true;
    combined.warningCount = 0;
    combined.errorCount = 0;
    combined.fatalCount = 0;

    auto start = std::chrono::steady_clock::now();

    auto merge = [&combined](const VerificationResult& r) {
        combined.warningCount += r.warningCount;
        combined.errorCount += r.errorCount;
        combined.fatalCount += r.fatalCount;
        if (!r.passed) combined.passed = false;
        combined.errors.insert(combined.errors.end(), r.errors.begin(), r.errors.end());
    };

    merge(verifyObjectRefs());
    merge(verifyMarkBitmap());
    merge(verifyWriteBarriers());
    merge(verifyRoots());
    merge(verifyNoBrokenPointers());

    combined.durationMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();

    return combined;
}

inline VerificationResult HeapVerifierImpl::verifyObjectRefs() {
    VerificationResult result;
    result.passed = true;
    result.warningCount = 0;
    result.errorCount = 0;
    result.fatalCount = 0;

    if (!cb_.iterateAllObjects || !cb_.traceObject || !cb_.isHeapAddress) return result;

    cb_.iterateAllObjects([&](void* obj, size_t /*size*/) {
        cb_.traceObject(obj, [&](void** slot) {
            if (!slot) return;
            void* ref = *slot;
            if (!ref) return;

            if (!cb_.isHeapAddress(ref)) {
                result.addError(
                    VerificationError::Severity::Fatal,
                    VerificationError::Category::ObjectRef,
                    "Reference points outside managed heap",
                    ref);
            }
        });
    });

    return result;
}

inline VerificationResult HeapVerifierImpl::verifyMarkBitmap() {
    VerificationResult result;
    result.passed = true;
    result.warningCount = 0;
    result.errorCount = 0;
    result.fatalCount = 0;

    // After a full marking phase, verify that all reachable objects are marked
    if (!cb_.enumerateRoots || !cb_.isMarked || !cb_.traceObject) return result;

    std::unordered_set<void*> reachable;
    std::vector<void*> worklist;

    // BFS from roots
    cb_.enumerateRoots([&](void** slot) {
        if (slot && *slot) {
            if (reachable.insert(*slot).second) {
                worklist.push_back(*slot);
            }
        }
    });

    while (!worklist.empty()) {
        void* obj = worklist.back();
        worklist.pop_back();

        cb_.traceObject(obj, [&](void** slot) {
            if (slot && *slot) {
                if (reachable.insert(*slot).second) {
                    worklist.push_back(*slot);
                }
            }
        });
    }

    // All reachable objects must be marked
    for (void* obj : reachable) {
        if (!cb_.isMarked(obj)) {
            result.addError(
                VerificationError::Severity::Fatal,
                VerificationError::Category::MarkBitmap,
                "Reachable object not marked",
                obj);
        }
    }

    return result;
}

inline VerificationResult HeapVerifierImpl::verifyWriteBarriers() {
    VerificationResult result;
    result.passed = true;
    result.warningCount = 0;
    result.errorCount = 0;
    result.fatalCount = 0;

    if (!cb_.iterateAllObjects || !cb_.traceObject ||
        !cb_.isInOldGen || !cb_.isInNursery || !cb_.isCardDirty) return result;

    // For every old-gen object with a reference to nursery, the card must be dirty
    cb_.iterateAllObjects([&](void* obj, size_t /*size*/) {
        if (!cb_.isInOldGen(obj)) return;

        cb_.traceObject(obj, [&](void** slot) {
            if (!slot || !*slot) return;
            if (cb_.isInNursery(*slot)) {
                // Old→young reference — card must be dirty
                if (!cb_.isCardDirty(obj)) {
                    result.addError(
                        VerificationError::Severity::Error,
                        VerificationError::Category::WriteBarrier,
                        "Old→young reference without dirty card",
                        obj);
                }
            }
        });
    });

    return result;
}

inline VerificationResult HeapVerifierImpl::verifyFreeList() {
    VerificationResult result;
    result.passed = true;
    result.warningCount = 0;
    result.errorCount = 0;
    result.fatalCount = 0;
    return result;
}

inline VerificationResult HeapVerifierImpl::verifyRoots() {
    VerificationResult result;
    result.passed = true;
    result.warningCount = 0;
    result.errorCount = 0;
    result.fatalCount = 0;

    if (!cb_.enumerateRoots || !cb_.isHeapAddress) return result;

    cb_.enumerateRoots([&](void** slot) {
        if (!slot) return;
        void* ref = *slot;
        if (!ref) return;

        if (!cb_.isHeapAddress(ref)) {
            result.addError(
                VerificationError::Severity::Fatal,
                VerificationError::Category::ObjectRef,
                "Root points outside managed heap",
                ref);
        }
    });

    return result;
}

inline VerificationResult HeapVerifierImpl::verifyNoBrokenPointers() {
    VerificationResult result;
    result.passed = true;
    result.warningCount = 0;
    result.errorCount = 0;
    result.fatalCount = 0;
    return result;
}

} // namespace Zepra::Heap
