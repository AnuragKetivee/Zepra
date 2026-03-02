# ZepraEngine CMakeLists.txt
# NOTE: All demos in this directory require SDL which has been removed from the project.
# The main browser is built from the root CMakeLists.txt, not here.
# This directory is kept for reference only.

cmake_minimum_required(VERSION 3.16)
project(ZepraEngine VERSION 1.0.0 LANGUAGES CXX)

message(STATUS "ZepraEngine: Demos disabled (SDL removed from project)")

# All demo executables have been disabled because they depend on SDL:
# - zepra-browser (SDL window demo)
# - gl-demo (SDL+OpenGL demo)
# - webgl-demo (SDL+WebGL demo)  
# - zepra-browser-app (SDL-based browser)
# - zepra-browser-nxgfx (requires nxrender C API)
#
# The main browser executable is built from the root CMakeLists.txt
# using NXRENDER C++ (USE_NXRENDER=ON)
