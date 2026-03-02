# =============================================================================
# ZepraScript Core Engine Library
# =============================================================================

set(ZEPRA_CORE_SOURCES
    # Frontend
    frontend/lexer.cpp
    frontend/parser.cpp
    frontend/ast.cpp
    frontend/source_code.cpp
    frontend/syntax_checker.cpp
    
    # Runtime
    runtime/value.cpp
    runtime/object.cpp
    runtime/function.cpp
    runtime/vm.cpp
    runtime/environment.cpp
    runtime/global_object.cpp
    runtime/module.cpp
    runtime/module_loader.cpp
    runtime/promise.cpp
    runtime/inline_cache.cpp
    runtime/iterator.cpp
    runtime/proxy.cpp
    runtime/class_fields.cpp
    runtime/async_function.cpp
    
    # Garbage Collector
    gc/heap.cpp
    gc/gc_heap.cpp
    gc/concurrent_gc.cpp
    gc/allocator.cpp
    gc/marking.cpp
    gc/sweeping.cpp
    gc/handle.cpp
    gc/write_barrier.cpp
    gc/incremental_gc.cpp
    gc/compacting.cpp
    gc/generational_gc.cpp
    gc/finalizer.cpp    
    # Bytecode
    bytecode/bytecode_generator.cpp
    bytecode/opcode.cpp
    bytecode/peephole_optimizer.cpp
    
    # Interpreter
    interpreter/interpreter.cpp
    interpreter/stack_frame.cpp
    
    # Builtins
    builtins/object_builtins.cpp
    builtins/array.cpp
    builtins/string.cpp
    builtins/number.cpp
    builtins/boolean.cpp
    builtins/math.cpp
    builtins/date.cpp
    builtins/console.cpp
    builtins/json.cpp
    builtins/map.cpp
    builtins/set.cpp
    builtins/regexp.cpp
    builtins/symbol.cpp
    builtins/weakmap.cpp
    builtins/reflect.cpp
    builtins/generator.cpp
    builtins/intl.cpp
    builtins/typed_array.cpp
    builtins/data_view.cpp
    builtins/temporal.cpp
    builtins/atomics.cpp
    
    # WebAssembly
    wasm/wasm.cpp
    wasm/WasmValidate.cpp
    wasm/WasmBaselineCompile.cpp
    wasm/WasmInterpreter.cpp
    
    # JIT
    jit/jit_profiler.cpp
    jit/type_profiler.cpp
    jit/baseline_jit.cpp
    jit/osr.cpp
    jit/macro_assembler.cpp
    jit/deoptimizer.cpp
    
    # DFG Optimizer
    dfg/DFGGraph.cpp
    dfg/DFGBuilder.cpp
    dfg/DFGLowering.cpp
    dfg/DFGPipeline.cpp
    
    # Memory
    memory/page_allocator.cpp
    memory/arena_allocator.cpp
    memory/memory_pool.cpp
    
    # API
    api/isolate.cpp
    api/context.cpp
    api/local_handle.cpp
    api/script.cpp
    api/value_api.cpp
    
    # Debug
    debug/debug_api.cpp
    debug/breakpoint_manager.cpp
    debug/call_stack_info.cpp
    debug/execution_control.cpp
    debug/debugger.cpp
    debug/inspector.cpp
    debug/profiler.cpp
    debug/console.cpp
    
    # Profiler
    profiler/cpu_profiler.cpp
    profiler/heap_profiler.cpp
    profiler/sampling_profiler.cpp
    profiler/timeline.cpp
    
    # Utils
    utils/platform.cpp
    utils/unicode.cpp
    utils/string_builder.cpp
    
    # Host
    host/script_engine.cpp
    host/native_function.cpp
    
    # Browser
    browser/window.cpp
    browser/document.cpp
    browser/fetch.cpp
    browser/url.cpp
    browser/worker.cpp
    browser/event_system.cpp
    browser/storage_api.cpp
    browser/secure_storage.cpp
    browser/password_vault.cpp
    browser/structured_clone.cpp
    browser/indexeddb.cpp
    browser/service_worker.cpp
    browser/websocket.cpp
    browser/performance.cpp
    browser/console_api.cpp
    browser/devtools.cpp

    # Regex Engine
    regex/regex_compiler.cpp
    regex/regex_engine.cpp

    # Threading
    threading/thread_pool.cpp

    # Optimization
    optimization/hidden_class.cpp
    optimization/inline_cache_opt.cpp
    optimization/property_table.cpp
    optimization/speculation.cpp
)

# Create core library
add_library(zepra-core STATIC ${ZEPRA_CORE_SOURCES})

# Determine nxhttp path - support both standalone and integrated builds
# CMAKE_CURRENT_SOURCE_DIR = .../zepraScript/src, so ../.. = .../source
set(ZEPRA_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
if(EXISTS "${CMAKE_SOURCE_DIR}/source/nxhttp/include")
    # Building as part of zeprabrowser (from root)
    set(NXHTTP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/source/nxhttp/include")
    set(NXHTTP_LIB "${CMAKE_SOURCE_DIR}/source/nxhttp/build/libnxhttp.a")
elseif(EXISTS "${ZEPRA_SOURCE_ROOT}/nxhttp/include")
    # Building zepraScript standalone - nxhttp is sibling dir
    set(NXHTTP_INCLUDE_DIR "${ZEPRA_SOURCE_ROOT}/nxhttp/include")
    set(NXHTTP_LIB "${ZEPRA_SOURCE_ROOT}/nxhttp/build/libnxhttp.a")
else()
    message(WARNING "nxhttp not found - fetch.cpp will fail to build")
    set(NXHTTP_INCLUDE_DIR "")
    set(NXHTTP_LIB "")
endif()

# Include directories (including nxhttp)
target_include_directories(zepra-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${NXHTTP_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link threading support
find_package(Threads REQUIRED)

# Link libraries
target_link_libraries(zepra-core
    PUBLIC
        Threads::Threads
)

# Conditionally link nxhttp if available
if(NXHTTP_LIB AND EXISTS "${NXHTTP_LIB}")
    target_link_libraries(zepra-core PUBLIC ${NXHTTP_LIB})
endif()

# Platform-specific settings
if(UNIX AND NOT APPLE)
    target_link_libraries(zepra-core PUBLIC dl)
endif()

# Add compile definitions
target_compile_definitions(zepra-core
    PRIVATE
        ZEPRA_VERSION="${PROJECT_VERSION}"
        ZEPRA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        ZEPRA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        ZEPRA_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
