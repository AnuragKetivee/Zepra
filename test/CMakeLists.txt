
# Test Directory CMake Configuration
message(STATUS "Configuring tests...")

# -----------------------------------------------------------------------------
# Test 1: Zepra Basic Test
# -----------------------------------------------------------------------------
add_executable(zepra_test test_basic.cpp 
    ${PROJECT_SOURCE_DIR}/src/search/ketivee_search.cpp
    ${PROJECT_SOURCE_DIR}/src/engine/html_parser_stub.cpp
)
# Note: ${HEADERS} variable scope might not be visible here if not cached. 
# Assuming headers are available or checking if HEADERS are actually needed for test_basic.cpp

if(SDL2_FOUND)
    if(SDL2_LIBRARIES)
        target_link_libraries(zepra_test ${SDL2_LIBRARIES})
        target_include_directories(zepra_test PRIVATE ${SDL2_INCLUDE_DIRS})
    else()
        target_link_libraries(zepra_test SDL2::SDL2)
    endif()
endif()

if(CURL_FOUND)
    if(CURL_LIBRARIES)
        target_link_libraries(zepra_test ${CURL_LIBRARIES})
        target_include_directories(zepra_test PRIVATE ${CURL_INCLUDE_DIRS})
    else()
        target_link_libraries(zepra_test CURL::libcurl)
    endif()
endif()

target_link_libraries(zepra_test ${OPENGL_LIBRARIES} ${PLATFORM_LIBS} zepra-core webcore zepra-integration)
target_compile_options(zepra_test PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# -----------------------------------------------------------------------------
# Test 2: Configuration Test
# -----------------------------------------------------------------------------
# Need config_manager.cpp path relative to root
add_executable(config_test config_test.cpp ${PROJECT_SOURCE_DIR}/src/config/config_manager.cpp)

target_include_directories(config_test PRIVATE 
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

target_compile_options(config_test PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

enable_testing()
add_test(NAME ZepraBasicTest COMMAND zepra_test)

# -----------------------------------------------------------------------------
# Test 4: Browser Test Runner (Comprehensive Test Suite)
# Following WebKit/Firefox testing patterns
# -----------------------------------------------------------------------------
add_executable(browser_test_runner browser_test_runner.cpp)

target_include_directories(browser_test_runner PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/source/webCore/include
    ${PROJECT_SOURCE_DIR}/source/zepraScript/include
)

target_link_libraries(browser_test_runner
    zepra-core
    webcore
    zepra-integration
    ${OPENGL_LIBRARIES}
    ${PLATFORM_LIBS}
)

target_compile_options(browser_test_runner PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

add_test(NAME BrowserTestRunner COMMAND browser_test_runner)

# -----------------------------------------------------------------------------
# Test 5: CSS Cascade Test Suite
# -----------------------------------------------------------------------------
add_executable(css_cascade_test css_cascade_test.cpp)

target_include_directories(css_cascade_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/source/webCore/include
)

target_link_libraries(css_cascade_test
    webcore
    ${PLATFORM_LIBS}
)

target_compile_options(css_cascade_test PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

add_test(NAME CSSCascadeTest COMMAND css_cascade_test)

# -----------------------------------------------------------------------------
# Test 3: Visual Test for NxSVG
# -----------------------------------------------------------------------------

if(USE_NXRENDER)
    add_executable(visual_test_nxsvg visual_test_nxsvg.cpp)
    target_link_libraries(visual_test_nxsvg 
        nxrender
        ${X11_LIBRARIES}
        GL GLX
        pthread dl m
        Freetype::Freetype
    )
    target_include_directories(visual_test_nxsvg PRIVATE 
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/source/nxrender-cpp/include
        ${X11_INCLUDE_DIR}
        ${FREETYPE_INCLUDE_DIRS}
    )
    message(STATUS "Added visual_test_nxsvg target")
endif()

# -----------------------------------------------------------------------------
# Test 6: nxhttp Security Test Suite (P2)
# Security-critical: malformed frames, invalid UTF-8, attack vectors
# -----------------------------------------------------------------------------
find_package(GTest QUIET)
if(GTest_FOUND)
    add_executable(nxhttp_security_test nxhttp_security_test.cpp)
    
    target_include_directories(nxhttp_security_test PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/source/nxhttp/include
        ${PROJECT_SOURCE_DIR}/source/networking/include
    )
    
    target_link_libraries(nxhttp_security_test
        nxhttp
        networking
        GTest::gtest
        GTest::gtest_main
        pthread
    )
    
    target_compile_options(nxhttp_security_test PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )
    
    add_test(NAME NxHttpSecurityTest COMMAND nxhttp_security_test)
    
    message(STATUS "Added nxhttp_security_test target (GTest found)")

    # -------------------------------------------------------------------------
    # Test 7: nxhttp URL Security Tests
    # -------------------------------------------------------------------------
    add_executable(nxhttp_tests nxhttp_tests.cpp)
    
    target_include_directories(nxhttp_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/source/nxhttp/include
        ${PROJECT_SOURCE_DIR}/source/networking/include
    )
    
    target_link_libraries(nxhttp_tests
        nxhttp
        networking
        GTest::gtest
        GTest::gtest_main
        pthread
    )
    
    add_test(NAME NxHttpTests COMMAND nxhttp_tests)
    message(STATUS "Added nxhttp_tests target")

    # -------------------------------------------------------------------------
    # Test 8: nxcrypto Security Tests
    # -------------------------------------------------------------------------
    add_executable(nxcrypto_tests nxcrypto_tests.cpp)
    
    target_include_directories(nxcrypto_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/source/nxcrypto/include
    )
    
    target_link_libraries(nxcrypto_tests
        nxcrypto
        GTest::gtest
        GTest::gtest_main
        pthread
        ssl
        crypto
    )
    
    add_test(NAME NxCryptoTests COMMAND nxcrypto_tests)
    message(STATUS "Added nxcrypto_tests target")

else()
    message(STATUS "GTest not found - security tests skipped")
endif()

# -----------------------------------------------------------------------------
# ZepraScript Tests (reorganized from source/zepraScript/tests/)
# -----------------------------------------------------------------------------
add_subdirectory(zeprascript)
