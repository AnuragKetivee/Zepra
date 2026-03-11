// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file microtask_drain_stub.cpp
 * @brief Provides the MicrotaskQueue::drain() symbol needed by pre-built webCore
 * 
 * The webCore library was compiled calling drain() but zepra-core only provides process().
 * This stub provides the missing symbol by forwarding to process().
 */

#include <zeprascript/runtime/promise.hpp>

namespace Zepra::Runtime {

void MicrotaskQueue::drain() {
    process();
}

} // namespace Zepra::Runtime
