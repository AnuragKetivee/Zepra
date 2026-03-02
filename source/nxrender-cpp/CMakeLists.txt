cmake_minimum_required(VERSION 3.28) # Dont change this
project(nxrender VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(NXRENDER_SOURCES
    # nxgfx
    src/nxgfx/color.cpp
    src/nxgfx/context.cpp
    src/nxgfx/context3d.cpp
    src/nxgfx/text.cpp
    
    # core
    src/core/compositor.cpp
    src/core/surface.cpp
    
    # widgets
    src/widgets/widget.cpp
    src/widgets/button.cpp
    src/widgets/label.cpp
    src/widgets/textfield.cpp
    src/widgets/container.cpp
    src/widgets/scrollview.cpp
    src/widgets/listview.cpp
    src/widgets/tabview.cpp
    
    # layout
    src/layout/flexbox.cpp
    
    # theme
    src/theme/theme.cpp
    
    # input
    src/input/mouse.cpp
    src/input/keyboard.cpp
    
    # animation
    src/animation/animator.cpp
    
    # platform
    src/platform/linux_x11.cpp
    
    # bridge to libnxrender.a (C library) - DISABLED: requires external nxgfx C lib
    # src/nxrender_c_bridge.cpp
    
    # main
    src/nxrender.cpp
)

# Create library
add_library(nxrender STATIC ${NXRENDER_SOURCES})

# Include directories
target_include_directories(nxrender PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Include C library headers for bridge (from nxrender_c)
target_include_directories(nxrender PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../gui/nxrender_c/include
)

# Link dependencies
find_package(OpenGL REQUIRED)
find_package(Freetype REQUIRED)

target_link_libraries(nxrender PUBLIC
    OpenGL::GL
    Freetype::Freetype
)

# Platform-specific
if(UNIX AND NOT APPLE)
    target_link_libraries(nxrender PUBLIC X11)
endif()

# Install
install(TARGETS nxrender
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Examples (optional)
option(BUILD_EXAMPLES "Build example applications" OFF)
if(BUILD_EXAMPLES)
    add_executable(browser_ui_example examples/browser_ui_example.cpp)
    target_link_libraries(browser_ui_example PRIVATE nxrender)
endif()
