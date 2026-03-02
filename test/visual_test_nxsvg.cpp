/*
 * Visual Test for NxSVG
 * Compiles with: g++ -o visual_test visual_test_nxsvg.cpp -Iinclude -lX11 -lGL -lGLX
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <unistd.h>
#include "nxgfx/context.h"
#include "nxsvg.h"

// Test SVGs
const std::string SVG_POLYLINE = R"(
<svg viewBox="0 0 100 100">
    <!-- Red Polyline -->
    <polyline points="10,10 40,40 10,70 40,90" fill="none" stroke="red" stroke-width="5" />
</svg>
)";

const std::string SVG_POLYGON = R"(
<svg viewBox="0 0 100 100">
    <!-- Green Polygon with opacity -->
    <polygon points="60,10 90,40 60,70 90,90" fill="green" fill-opacity="0.5" stroke="blue" stroke-width="3" />
</svg>
)";

const std::string SVG_COMPLEX = R"(
<svg viewBox="0 0 100 100">
    <!-- Circle and Rect for regression -->
    <circle cx="50" cy="50" r="20" fill="yellow" stroke="black" />
    <rect x="10" y="40" width="80" height="20" fill="none" stroke="purple" stroke-width="2" />
</svg>
)";

int main() {

    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) {
        std::cerr << "Cannot open display" << std::endl;
        return 1;
    }

    Window root = DefaultRootWindow(dpy);
    // Request MSAA (4 samples)
    GLint att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8, GLX_DOUBLEBUFFER, 
                    GLX_SAMPLE_BUFFERS, 1, GLX_SAMPLES, 4, None };
    XVisualInfo* vi = glXChooseVisual(dpy, 0, att);
    if (!vi) {
        // Fallback to non-MSAA if 4x fails
        std::cerr << "Failed to get 4x MSAA visual, trying default..." << std::endl;
        GLint att_fallback[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8, GLX_DOUBLEBUFFER, None };
        vi = glXChooseVisual(dpy, 0, att_fallback);
    }
    if (!vi) {
        std::cerr << "No appropriate visual found" << std::endl;
        return 1;
    }

    Colormap cmap = XCreateColormap(dpy, root, vi->visual, AllocNone);
    XSetWindowAttributes swa;
    swa.colormap = cmap;
    swa.event_mask = ExposureMask | KeyPressMask;

    Window win = XCreateWindow(dpy, root, 0, 0, 800, 600, 0, vi->depth, InputOutput, vi->visual, CWColormap | CWEventMask, &swa);
    XMapWindow(dpy, win);
    XStoreName(dpy, win, "NxSVG Visual Test");

    GLXContext glc = glXCreateContext(dpy, vi, NULL, GL_TRUE);
    glXMakeCurrent(dpy, win, glc);
    
    // Enable Multisample
    glEnable(GL_MULTISAMPLE);

    std::cout << "GL Context Created" << std::endl;
    // Initialize NXRender GpuContext
    NXRender::GpuContext gpuContext;
    if (!gpuContext.init(800, 600)) {
        std::cerr << "Failed to initialize GpuContext" << std::endl;
        return 1;
    }

    // Load SVGs from resources
    nxsvg::SvgLoader loader;
    
    // Load icons that were known to be problematic or common
    loader.loadFromFile("back", "resources/icons/arrow-back.svg");
    loader.loadFromFile("forward", "resources/icons/arrow-forward.svg");
    loader.loadFromFile("refresh", "resources/icons/refresh.svg");
    loader.loadFromFile("search", "resources/icons/search.svg");
    loader.loadFromFile("shield", "resources/icons/shield.svg");
    loader.loadFromFile("close", "resources/icons/close.svg");
    loader.loadFromFile("logo", "resources/icons/zepralogo.svg");

    std::cout << "Visual test running. Loading icons from resources/icons/..." << std::endl;

    bool running = true;
    while (running) {
        XEvent xev;
        if (XPending(dpy)) {
            XNextEvent(dpy, &xev);
            if (xev.type == KeyPress) {
                running = false;
            }
        }

        // Render
        glViewport(0, 0, 800, 600);
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f); // Dark background to see light icons
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, 800, 600, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // Draw labels and SVGs in a grid
        int x = 50, y = 50;
        int size = 64;
        int gap = 100;

        // Row 1
        loader.draw("back", x, y, size);
        loader.draw("forward", x + gap, y, size);
        loader.draw("refresh", x + gap*2, y, size);
        
        // Row 2
        y += gap;
        loader.draw("search", x, y, size);
        loader.draw("shield", x + gap, y, size, 0, 255, 0); // Green shield
        loader.draw("close", x + gap*2, y, size);
        
        // Row 3
        y += gap;
        loader.draw("logo", x, y, size * 2); // Draw logo larger

        glXSwapBuffers(dpy, win);
        
        // Capture screenshot and exit (for verification)
        static bool captured = false;
        if (!captured) {
            std::vector<uint8_t> pixels(800 * 600 * 3);
            glReadPixels(0, 0, 800, 600, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            
            std::ofstream ppm("nxsvg_test_output.ppm");
            ppm << "P3\n800 600\n255\n";
            for (int y = 599; y >= 0; --y) {
                for (int x = 0; x < 800; ++x) {
                    size_t i = (y * 800 + x) * 3;
                    ppm << (int)pixels[i] << " " << (int)pixels[i+1] << " " << (int)pixels[i+2] << "\n";
                }
            }
            std::cout << "Screenshot saved to nxsvg_test_output.ppm" << std::endl;
            captured = true;
            // running = false; // Uncomment to exit after one frame
        }
        
        usleep(16000); // ~60fps
    }

    glXMakeCurrent(dpy, None, NULL);
    glXDestroyContext(dpy, glc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);

    return 0;
}
