/**
 * @file WasmStackManager.cpp
 * @brief WebAssembly Stack Overflow Protection Implementation
 *
 * Manages per-thread stack limits and guard regions for safe WASM execution.
 * Prevents stack overflow in WASM code from crashing the process.
 */

#include "zeprascript/wasm/WasmStackManager.h"

#ifdef __linux__
#include <pthread.h>
#include <sys/resource.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

#include <cstdint>

namespace Zepra::Wasm {

thread_local uintptr_t StackManager::threadLimit_ = 0;

void StackManager::initThreadStack(size_t stackSize) {
    // Get current stack pointer
    uintptr_t sp;
#if defined(__x86_64__) || defined(_M_X64)
    asm volatile("mov %%rsp, %0" : "=r"(sp));
#elif defined(__aarch64__) || defined(_M_ARM64)
    asm volatile("mov %0, sp" : "=r"(sp));
#else
    // Fallback: use address of local variable
    volatile int dummy;
    sp = reinterpret_cast<uintptr_t>(&dummy);
#endif

    if (stackSize == 0) {
        // Detect actual stack size from OS
#ifdef __linux__
        struct rlimit rl;
        if (getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
            stackSize = static_cast<size_t>(rl.rlim_cur);
        } else {
            stackSize = 8 * 1024 * 1024; // Default 8MB
        }
#elif defined(__APPLE__)
        pthread_t self = pthread_self();
        stackSize = pthread_get_stacksize_np(self);
#else
        stackSize = 8 * 1024 * 1024; // Default 8MB
#endif
    }

    // Reserve guard region (64KB) at the bottom of the stack
    // Stack grows downward: limit = sp - stackSize + guardSize
    constexpr size_t GUARD_SIZE = 64 * 1024;
    threadLimit_ = sp - stackSize + GUARD_SIZE;
}

} // namespace Zepra::Wasm
