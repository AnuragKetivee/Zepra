# WebCore CMakeLists.txt
# NOTE: Source files are missing - using pre-built library if available
cmake_minimum_required(VERSION 3.16)
project(WebCore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check if pre-built library exists
set(PREBUILT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/build/libwebcore.a")

if(EXISTS ${PREBUILT_LIB})
    # Use pre-built library (source files are missing)
    message(STATUS "WebCore: Using pre-built library ${PREBUILT_LIB}")
    
    # Create interface library for includes (IMPORTED targets don't propagate includes well)
    add_library(webcore INTERFACE)
    
    # Link the pre-built static library
    target_link_libraries(webcore INTERFACE ${PREBUILT_LIB})
    
    # Set include directories - these will be propagated to consumers
    target_include_directories(webcore INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../zepraScript/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../networking/include
        ${CMAKE_SOURCE_DIR}/source/nxrender-cpp/include
        ${CMAKE_SOURCE_DIR}/include
    )
    
    # Find and link dependencies that the pre-built library needs
    find_package(OpenGL QUIET)
    if(OpenGL_FOUND)
        target_link_libraries(webcore INTERFACE OpenGL::GL)
        target_compile_definitions(webcore INTERFACE ZEPRA_HAS_OPENGL)
    endif()
    
    # Link with zepra-core and networking if available (these were linked when building libwebcore.a)
    if(TARGET zepra-core)
        target_link_libraries(webcore INTERFACE zepra-core)
    endif()
    if(TARGET networking)
        target_link_libraries(webcore INTERFACE networking)
    endif()
    
else()
    # Source-based build (requires source files in src/)
    message(STATUS "WebCore: Building from source")
    
    # Include directories
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
    
    # ZepraScript include path
    set(ZEPRASCRIPT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zepraScript)
    include_directories(${ZEPRASCRIPT_DIR}/include)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../networking/include)
    
    # Include main include directory for nxsvg.h, nxrender.h, nxfont.h
    include_directories(${CMAKE_SOURCE_DIR}/include)
    
    # Source files - check which ones exist
    set(WEBCORE_SOURCES "")
    
    # Core sources
    foreach(src
        src/dom.cpp
        src/render_tree.cpp
        src/paint_context.cpp
        src/js_dom_bridge.cpp
        src/layout_engine.cpp
        src/html_parser.cpp
        src/browser_ui.cpp
        src/page.cpp
        src/event.cpp
        src/event_target.cpp
        src/scrollbar.cpp
        src/resource_loader.cpp
        src/script_context.cpp
        src/context_menu.cpp
        src/gl_render_backend.cpp
        src/script_loader.cpp
        src/js_realm.cpp
        src/gc_bridge.cpp
        src/webgl_context.cpp
        src/webgl_bindings.cpp
        src/nxgfx_context.cpp
        src/simple_font.cpp
        src/shortcut_manager.cpp
        src/sidebar.cpp
        src/svg_loader.cpp
        src/navigation_history.cpp
        src/compositor.cpp
        src/page_renderer.cpp
        # HTML Elements
        src/html/html_element.cpp
        src/html/html_dialog_element.cpp
        src/html/html_anchor_element.cpp
        src/html/html_div_element.cpp
        src/html/html_span_element.cpp
        src/html/html_image_element.cpp
        src/html/html_form_element.cpp
        src/html/html_input_element.cpp
        src/html/html_button_element.cpp
        src/html/html_media_element.cpp
        src/html/html_canvas_element.cpp
        # New HTML Element implementations
        src/html/html_link_element.cpp
        src/html/html_meta_element.cpp
        src/html/html_style_element.cpp
        src/html/html_structural_elements.cpp
        src/html/html_text_elements.cpp
        src/html/html_form_extras.cpp
        src/html/html_media_extras.cpp
        src/html/html_text_formatting.cpp
        src/html/html_element_factory.cpp
        # Browser Interfaces
        src/browser/window.cpp
        src/browser/navigator.cpp
        src/browser/location.cpp
        # CSS Engine
        src/css/css_style_sheet.cpp
        src/css/css_style_declaration.cpp
        src/css/css_rule.cpp
        src/css/css_selector.cpp
        src/css/css_computed_style.cpp
        src/css/css_engine.cpp
        src/css/css_cascade.cpp
        src/css/style_resolver.cpp
    )
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
            list(APPEND WEBCORE_SOURCES ${src})
        endif()
    endforeach()
    
    if(WEBCORE_SOURCES)
        # Find OpenGL for GPU rendering
        find_package(OpenGL QUIET)
        
        # Library target
        add_library(webcore STATIC ${WEBCORE_SOURCES})
        
        target_include_directories(webcore PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../zepraScript/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../networking/include
            ${CMAKE_SOURCE_DIR}/source/nxrender-cpp/include
            ${CMAKE_SOURCE_DIR}/include
        )
        
        # Link with ZepraScript (if available)
        if(TARGET zepra-core)
            target_link_libraries(webcore PUBLIC zepra-core)
            target_compile_definitions(webcore PUBLIC USE_ZEPRASCRIPT)
        endif()
        
        # Link with Networking
        if(TARGET networking)
            target_link_libraries(webcore PUBLIC networking)
        endif()
        
        if(OpenGL_FOUND)
            target_link_libraries(webcore PUBLIC OpenGL::GL)
            target_compile_definitions(webcore PUBLIC ZEPRA_HAS_OPENGL)
        endif()
        
        # Export settings
        set_target_properties(webcore PROPERTIES
            POSITION_INDEPENDENT_CODE ON
        )
    else()
        message(FATAL_ERROR "WebCore: No source files found and no pre-built library available. "
                           "Please either restore source files in src/ or the pre-built library.")
    endif()
endif()
