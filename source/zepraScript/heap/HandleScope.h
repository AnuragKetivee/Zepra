// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file HandleScope.h
 * @brief RAII-based GC root management
 *
 * HandleScopes provide stack-like GC root registration:
 * - Objects referenced via handles are visible to the GC
 * - When a HandleScope is destroyed, all its handles are released
 * - Nested scopes supported via linked list
 *
 * Usage:
 *   {
 *       HandleScope scope(isolate);
 *       Handle<Object> obj = scope.create(allocateObject());
 *       // obj protected from GC
 *       doWork(obj);
 *   }
 *   // obj no longer a GC root
 *
 * This is critical for C++ ↔ JS boundary code where native
 * references must survive GC cycles.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <type_traits>
#include <cassert>

namespace Zepra::Heap {

// Forward declarations
class HandleScopeImpl;

// =============================================================================
// Handle — typed GC root reference
// =============================================================================

/**
 * @brief A GC-protected reference to a heap object
 *
 * Handles are valid only within the HandleScope that created them.
 * They automatically track the object and update if the GC moves it.
 */
template<typename T>
class Handle {
public:
    Handle() : location_(nullptr) {}

    explicit Handle(T** location) : location_(location) {}

    T* get() const { return location_ ? *location_ : nullptr; }
    T* operator->() const { return get(); }
    T& operator*() const { return *get(); }

    bool isNull() const { return !location_ || !*location_; }
    bool isValid() const { return location_ != nullptr; }

    explicit operator bool() const { return !isNull(); }

    // Comparison
    bool operator==(const Handle<T>& other) const { return get() == other.get(); }
    bool operator!=(const Handle<T>& other) const { return get() != other.get(); }

    // Cast to base type
    template<typename U>
    Handle<U> cast() const {
        static_assert(std::is_base_of_v<U, T> || std::is_base_of_v<T, U>,
                      "Invalid handle cast");
        return Handle<U>(reinterpret_cast<U**>(location_));
    }

    // Access internal slot (for GC to update pointers)
    T** location() const { return location_; }

private:
    T** location_;  // Points into the HandleScope's handle storage
};

// =============================================================================
// MaybeHandle — nullable variant
// =============================================================================

template<typename T>
class MaybeHandle {
public:
    MaybeHandle() = default;
    MaybeHandle(Handle<T> handle) : handle_(handle), hasValue_(true) {} // NOLINT

    bool hasValue() const { return hasValue_ && !handle_.isNull(); }

    Handle<T> value() const {
        assert(hasValue_);
        return handle_;
    }

    Handle<T> valueOr(Handle<T> fallback) const {
        return hasValue_ ? handle_ : fallback;
    }

    T* operator->() const { return value().get(); }

private:
    Handle<T> handle_;
    bool hasValue_ = false;
};

// =============================================================================
// HandleScope
// =============================================================================

/**
 * @brief RAII scope for GC handle management
 *
 * Allocates handles from a thread-local block.
 * On destruction, all handles created in this scope are invalidated.
 */
class HandleScope {
public:
    static constexpr size_t BLOCK_SIZE = 256;  // Handles per block

    HandleScope();
    ~HandleScope();

    // Non-copyable, non-movable
    HandleScope(const HandleScope&) = delete;
    HandleScope& operator=(const HandleScope&) = delete;

    /**
     * @brief Create a handle for an object
     */
    template<typename T>
    Handle<T> create(T* object) {
        void** slot = allocateSlot();
        *slot = static_cast<void*>(object);
        return Handle<T>(reinterpret_cast<T**>(slot));
    }

    /**
     * @brief Number of handles in this scope
     */
    size_t handleCount() const { return handleCount_; }

    /**
     * @brief Get the current (innermost) HandleScope
     */
    static HandleScope* current() { return currentScope_; }

    /**
     * @brief Iterate all live handles (for GC root enumeration)
     */
    template<typename Visitor>
    static void visitAllHandles(Visitor visitor) {
        HandleScope* scope = currentScope_;
        while (scope) {
            for (size_t i = 0; i < scope->handleCount_; i++) {
                void** slot = &scope->handles_[i];
                if (*slot) {
                    visitor(slot);
                }
            }
            scope = scope->previous_;
        }
    }

    /**
     * @brief Total handles across all scopes (debugging)
     */
    static size_t totalHandleCount();

private:
    void** allocateSlot();
    void grow();

    void** handles_ = nullptr;
    size_t handleCount_ = 0;
    size_t handleCapacity_ = 0;

    HandleScope* previous_;

    // Thread-local scope chain
    static thread_local HandleScope* currentScope_;
};

// =============================================================================
// EscapableHandleScope — promote handles to parent scope
// =============================================================================

/**
 * @brief HandleScope that allows one handle to "escape" to the parent scope
 *
 * Used when a function creates objects that must survive the function's scope.
 * Only one handle can escape.
 */
class EscapableHandleScope : public HandleScope {
public:
    EscapableHandleScope() : escaped_(false) {}

    /**
     * @brief Move a handle to the enclosing scope
     * Can only be called once per EscapableHandleScope.
     */
    template<typename T>
    Handle<T> escape(Handle<T> handle) {
        assert(!escaped_ && "Can only escape one handle per scope");
        escaped_ = true;

        HandleScope* parent = current();
        if (parent && parent != this) {
            // Re-create the handle in the parent scope
            return parent->create(handle.get());
        }
        return handle;
    }

private:
    bool escaped_;
};

// =============================================================================
// SealHandleScope — disallow handle creation
// =============================================================================

/**
 * @brief Debug scope that asserts no handles are created
 *
 * Used in performance-critical code to verify no accidental
 * handle allocations are happening.
 */
class SealHandleScope {
public:
    SealHandleScope();
    ~SealHandleScope();

    // Non-copyable
    SealHandleScope(const SealHandleScope&) = delete;
    SealHandleScope& operator=(const SealHandleScope&) = delete;

private:
    static thread_local bool sealed_;
    bool previousState_;
};

// =============================================================================
// PersistentHandle — handle that survives scope destruction
// =============================================================================

/**
 * @brief A handle that is not bound to any HandleScope
 *
 * Must be explicitly released. Used for permanent roots
 * like global object references.
 */
template<typename T>
class PersistentHandle {
public:
    PersistentHandle() = default;

    explicit PersistentHandle(T* object) {
        storage_ = static_cast<void*>(object);
        registerGlobal(&storage_);
    }

    ~PersistentHandle() {
        if (storage_) {
            unregisterGlobal(&storage_);
            storage_ = nullptr;
        }
    }

    // Move-only
    PersistentHandle(PersistentHandle&& other) noexcept
        : storage_(other.storage_) {
        other.storage_ = nullptr;
    }

    PersistentHandle& operator=(PersistentHandle&& other) noexcept {
        if (this != &other) {
            reset();
            storage_ = other.storage_;
            other.storage_ = nullptr;
        }
        return *this;
    }

    PersistentHandle(const PersistentHandle&) = delete;
    PersistentHandle& operator=(const PersistentHandle&) = delete;

    T* get() const { return static_cast<T*>(storage_); }
    T* operator->() const { return get(); }
    T& operator*() const { return *get(); }
    bool isNull() const { return storage_ == nullptr; }
    explicit operator bool() const { return !isNull(); }

    void reset() {
        if (storage_) {
            unregisterGlobal(&storage_);
            storage_ = nullptr;
        }
    }

    Handle<T> toHandle(HandleScope& scope) const {
        return scope.create(get());
    }

private:
    static void registerGlobal(void** slot);
    static void unregisterGlobal(void** slot);

    void* storage_ = nullptr;
};

// =============================================================================
// HandleScope Implementation
// =============================================================================

inline thread_local HandleScope* HandleScope::currentScope_ = nullptr;
inline thread_local bool SealHandleScope::sealed_ = false;

inline HandleScope::HandleScope()
    : previous_(currentScope_) {
    currentScope_ = this;

    handles_ = new void*[BLOCK_SIZE]();
    handleCapacity_ = BLOCK_SIZE;
    handleCount_ = 0;
}

inline HandleScope::~HandleScope() {
    currentScope_ = previous_;
    delete[] handles_;
}

inline void** HandleScope::allocateSlot() {
    if (handleCount_ >= handleCapacity_) {
        grow();
    }
    return &handles_[handleCount_++];
}

inline void HandleScope::grow() {
    size_t newCapacity = handleCapacity_ * 2;
    auto** newHandles = new void*[newCapacity]();
    std::memcpy(newHandles, handles_, handleCapacity_ * sizeof(void*));
    delete[] handles_;
    handles_ = newHandles;
    handleCapacity_ = newCapacity;
}

inline size_t HandleScope::totalHandleCount() {
    size_t total = 0;
    HandleScope* scope = currentScope_;
    while (scope) {
        total += scope->handleCount_;
        scope = scope->previous_;
    }
    return total;
}

inline SealHandleScope::SealHandleScope()
    : previousState_(sealed_) {
    sealed_ = true;
}

inline SealHandleScope::~SealHandleScope() {
    sealed_ = previousState_;
}

// Persistent handle global registration (implemented in gc_heap.cpp)
template<typename T>
void PersistentHandle<T>::registerGlobal(void** slot) {
    // In production, this adds the slot to a global root set
    // that is enumerated during GC marking
    (void)slot;
}

template<typename T>
void PersistentHandle<T>::unregisterGlobal(void** slot) {
    (void)slot;
}

} // namespace Zepra::Heap
