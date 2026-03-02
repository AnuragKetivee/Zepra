#!/bin/bash
# Build ZepraBrowser with FULL WebCore (CSS + JS)
# Creates /tmp/zepra_native with complete HTML/CSS/JavaScript support

set -e

BROWSER_DIR="/home/swana/Documents/NEOLYXOS/neolyx-os/apps/zeprabrowser"
ZEPRA_LIB="$BROWSER_DIR/source/zepraScript/build/lib/libzepra-core.a"
WEBCORE_LIB="$BROWSER_DIR/source/webCore/build/libwebcore.a"
NXRENDER_LIB="$BROWSER_DIR/source/nxrender-cpp/build/libnxrender.a"
NETWORKING_LIB="$BROWSER_DIR/source/networking/build/libnetworking.a"

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║     ZepraBrowser - Full WebCore Build                        ║"
echo "║     CSS Engine + JavaScript (ZebraScript) + HTML Parser      ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# Check libraries exist
if [ ! -f "$ZEPRA_LIB" ]; then
    echo "[!] Building ZebraScript..."
    cd "$BROWSER_DIR/source/zepraScript"
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DZEPRA_BUILD_TESTS=OFF -DZEPRA_BUILD_TOOLS=OFF
    make zepra-core -j2
fi

if [ ! -f "$WEBCORE_LIB" ]; then
    echo "[!] Building WebCore..."
    cd "$BROWSER_DIR/source/webCore"
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-I$BROWSER_DIR/include"
    make webcore -j2
fi

echo "[✓] Libraries ready:"
echo "    libzepra-core.a: $(du -h $ZEPRA_LIB | cut -f1)"
echo "    libwebcore.a: $(du -h $WEBCORE_LIB | cut -f1)"
echo ""

# Include paths
INCLUDES="-I$BROWSER_DIR/src \
    -I$BROWSER_DIR/include \
    -I$BROWSER_DIR/source/webCore/include \
    -I$BROWSER_DIR/source/zepraScript/include \
    -I$BROWSER_DIR/source/networking/include \
    -I$BROWSER_DIR/source/nxrender-cpp/include \
    -I$BROWSER_DIR/source/nxhttp/include \
    -I$BROWSER_DIR/source/nxbase/include \
    -I$BROWSER_DIR/source/nxjson/include \
    -I$BROWSER_DIR/source/nxxml/include \
    -I$BROWSER_DIR/source/nxcrypto/include \
    -I/usr/include/freetype2"

# Compile NX libraries
echo "[0.5/5] Compiling NX libraries..."
cd /tmp
g++ -c $BROWSER_DIR/source/nxbase/src/*.cpp $INCLUDES -std=c++20 -O2
g++ -c $BROWSER_DIR/source/nxhttp/src/*.cpp $INCLUDES -std=c++20 -O2

# Compile NXRender Core and Submodules
echo "[0.6/5] Compiling NXRender Core..."
find $BROWSER_DIR/source/nxrender-cpp/src -name "*.cpp" -exec g++ -c {} $INCLUDES -std=c++20 -O2 \;
mv *.o /tmp/ 2>/dev/null || true

# Compile NXRender Platform (Linux X11) - Done by find above if in src, but explicit safety for platform
# Actually platform is in src/platform, so it is caught by find.
# But we need platform specific flags? Platform/linux_x11.cpp needs X11 headers which are global.
# So generic compilation is fine.

# Create libnxrender.a (force create)
rm -f "$NXRENDER_LIB"
ar rcs "$NXRENDER_LIB" /tmp/*.o

# Compile main browser + webcore integration
echo "[1/5] Compiling zepra_browser.cpp..."
g++ -c "$BROWSER_DIR/src/zepra_browser.cpp" -o /tmp/zepra_browser.o \
    $INCLUDES -std=c++20 -O2 -DUSE_WEBCORE

echo "[2/5] Compiling webcore_integration.cpp..."
g++ -c "$BROWSER_DIR/src/webcore_integration.cpp" -o /tmp/webcore_integration.o \
    $INCLUDES -std=c++20 -O2

echo "[3/5] Compiling webgl_stubs.cpp (no SDL)..."
g++ -c "$BROWSER_DIR/src/webgl_stubs.cpp" -o /tmp/webgl_stubs.o \
    $INCLUDES -std=c++20 -O2

echo "[3.5/5] Compiling microtask stubs..."
g++ -c "$BROWSER_DIR/src/stubs/microtask_drain_stub.cpp" -o /tmp/microtask_drain_stub.o \
    $INCLUDES -std=c++20 -O2

echo "[4/6] Compiling modular browser components..."
g++ -c "$BROWSER_DIR/src/browser/layout_engine.cpp" -o /tmp/layout_engine.o \
    $INCLUDES -std=c++20 -O2
g++ -c "$BROWSER_DIR/src/browser/tab_manager.cpp" -o /tmp/tab_manager.o \
    $INCLUDES -std=c++20 -O2
g++ -c "$BROWSER_DIR/src/browser/mouse_handler.cpp" -o /tmp/mouse_handler.o \
    $INCLUDES -std=c++20 -O2
g++ -c "$BROWSER_DIR/src/browser/webcore_tab.cpp" -o /tmp/webcore_tab.o \
    $INCLUDES -std=c++20 -O2 -DUSE_WEBCORE
g++ -c "$BROWSER_DIR/src/browser/css_utils.cpp" -o /tmp/css_utils.o \
    $INCLUDES -std=c++20 -O2 -DUSE_WEBCORE
g++ -c "$BROWSER_DIR/src/engine/download_manager.cpp" -o /tmp/download_manager.o \
    $INCLUDES -std=c++20 -O2 || echo "download_manager skipped"
g++ -c "$BROWSER_DIR/src/browser/tab_suspender.cpp" -o /tmp/tab_suspender.o \
    $INCLUDES -std=c++20 -O2 || echo "tab_suspender skipped"
g++ -c "$BROWSER_DIR/src/browser/lazy_image_loader.cpp" -o /tmp/lazy_image_loader.o \
    $INCLUDES -std=c++20 -O2 || echo "lazy_image_loader skipped"

echo "[5/6] Linking..."
# We explicitly list object files to avoid duplicates or missing ones
g++ /tmp/zepra_browser.o /tmp/webcore_integration.o /tmp/webgl_stubs.o \
    /tmp/layout_engine.o /tmp/tab_manager.o /tmp/mouse_handler.o /tmp/webcore_tab.o \
    /tmp/css_utils.o \
    /tmp/microtask_drain_stub.o \
    /tmp/allocator.o /tmp/buffer.o /tmp/log.o /tmp/string.o \
    /tmp/client.o /tmp/headers.o /tmp/url.o \
    $(ls /tmp/download_manager.o /tmp/tab_suspender.o /tmp/lazy_image_loader.o 2>/dev/null) \
    "$NXRENDER_LIB" "$WEBCORE_LIB" "$ZEPRA_LIB" "$NETWORKING_LIB" \
    -o /tmp/zepra_native \
    -lX11 -lGL -lGLU -lfreetype -lcurl -lssl -lcrypto -lpthread -lz \
    -std=c++20 -O2

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  ✓ BUILD SUCCESSFUL                                          ║"
echo "║                                                               ║"
echo "║  Features:                                                    ║"
echo "║    • HTML Parser (full DOM tree)                              ║"
echo "║    • CSS Engine (cascade, specificity, computed styles)       ║"
echo "║    • JavaScript (ZebraScript VM with DOM bindings)            ║"
echo "║    • HTTP client (libcurl)                                    ║"
echo "║                                                               ║"
echo "║  Run: /tmp/zepra_native                                       ║"
echo "╚══════════════════════════════════════════════════════════════╝"
