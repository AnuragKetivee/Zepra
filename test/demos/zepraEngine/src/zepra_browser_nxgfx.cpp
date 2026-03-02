/**
 * @file zepra_browser_nxgfx.cpp
 * @brief Main Zepra Browser Application - SDL-FREE version
 * 
 * Uses NXGFXContext instead of SDL for all rendering and input.
 */

#include "webcore/nxgfx_context.hpp"
#include "webcore/browser_ui.hpp"
#include "webcore/page.hpp"
#include "webcore/context_menu.hpp"
#include "webcore/simple_font.hpp"
#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>

// NXGFX C ABI
extern "C" {
#include "nxrender.h"
}

using namespace Zepra::WebCore;

class NXBrowserApp {
public:
    NXBrowserApp() : chrome_(1024, 768), zoomLevel_(1.0f) {}
    
    bool init() {
        if (!context_.initialize("Zepra Browser", 1024, 768, NXRenderMode::Auto)) {
            std::cerr << "Failed to initialize NXGFX context" << std::endl;
            return false;
        }
        
        std::cout << "NXGFX context initialized" << std::endl;
        
        setupUIEvents();
        newTab("Welcome", "zepra://start");
        
        std::cout << "Init complete, entering event loop" << std::endl;
        return true;
    }
    
    void run() {
        running_ = true;
        int frameCount = 0;
        
        std::cout << "Starting event loop..." << std::endl;

        while (running_) {
            // Poll input from NXGFX
            pollEvents();
            
            update();
            render();
            
            frameCount++;
            if (frameCount == 1) {
                std::cout << "First frame rendered!" << std::endl;
            }
            if (frameCount == 10) {
                std::cout << "10 frames rendered, browser running normally..." << std::endl;
            }
            
            // Sleep ~16ms for ~60 FPS
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        std::cout << "Event loop ended after " << frameCount << " frames" << std::endl;
    }
    
    void quit() { running_ = false; }

private:
    void pollEvents() {
        // Get mouse state from NXGFX
        NxMouseState* mouse = context_.mouse();
        if (!mouse) return;
        
        float mx, my;
        nx_mouse_get_position(mouse, &mx, &my);
        
        // Track mouse position changes
        if (mx != lastMouseX_ || my != lastMouseY_) {
            handleMouseMove(static_cast<int>(mx), static_cast<int>(my));
            lastMouseX_ = mx;
            lastMouseY_ = my;
        }
        
        // Check mouse buttons
        bool leftDown = nx_mouse_is_button_down(mouse, NX_MOUSE_LEFT);
        if (leftDown && !wasLeftDown_) {
            handleMouseDown(static_cast<int>(mx), static_cast<int>(my), 1);
        } else if (!leftDown && wasLeftDown_) {
            handleMouseUp(static_cast<int>(mx), static_cast<int>(my), 1);
        }
        wasLeftDown_ = leftDown;
        
        bool rightDown = nx_mouse_is_button_down(mouse, NX_MOUSE_RIGHT);
        if (rightDown && !wasRightDown_) {
            handleMouseDown(static_cast<int>(mx), static_cast<int>(my), 3);
        } else if (!rightDown && wasRightDown_) {
            handleMouseUp(static_cast<int>(mx), static_cast<int>(my), 3);
        }
        wasRightDown_ = rightDown;
        
        // Check keyboard modifiers
        NxKeyboardState* kb = context_.keyboard();
        if (kb) {
            bool ctrl = nx_keyboard_is_ctrl(kb);
            bool shift = nx_keyboard_is_shift(kb);
            bool alt = nx_keyboard_is_alt(kb);
            
            // Handle Ctrl+Q to quit
            if (ctrl && !lastCtrl_) {
                // Check for quit shortcut
                // In real impl: check for specific key codes
            }
            lastCtrl_ = ctrl;
            lastShift_ = shift;
            lastAlt_ = alt;
        }
    }
    
    void handleMouseMove(int x, int y) {
        if (contextMenu_.isVisible()) {
            contextMenu_.handleMouseMove(x, y);
        }
        chrome_.handleMouseMove(x, y);
    }
    
    void handleMouseDown(int x, int y, int button) {
        if (contextMenu_.isVisible()) {
            if (contextMenu_.handleMouseDown(x, y, button)) {
                return;
            }
        }
        
        // Right-click opens context menu
        if (button == 3) {
            showContextMenu(x, y);
            return;
        }
        
        chrome_.handleMouseDown(x, y, button);
        if (activePage_) {
            auto area = chrome_.contentArea();
            if (y > area.y) {
                activePage_->hitTest(x, y);
            }
        }
    }
    
    void handleMouseUp(int x, int y, int button) {
        chrome_.handleMouseUp(x, y, button);
    }
    
    void setupUIEvents() {
        auto* nav = chrome_.navigationBar();
        auto* tabs = chrome_.tabBar();
        auto* sidebar = chrome_.sidebar();
        
        nav->setOnNavigate([this](const std::string& url) { navigate(url); });
        nav->setOnBack([this]() { std::cout << "Back clicked" << std::endl; });
        nav->setOnForward([this]() { std::cout << "Forward clicked" << std::endl; });
        nav->setOnRefresh([this]() { if (activePage_) activePage_->loadFromURL(activePage_->url()); });
        nav->setOnHome([this]() { navigate("zepra://start"); });
        nav->setOnMenu([this]() { std::cout << "Menu clicked" << std::endl; });
        
        tabs->setOnTabSwitch([this](int index) { switchTab(index); });
        tabs->setOnNewTab([this]() { newTab("New Tab", "about:blank"); });
        tabs->setOnCloseTab([this](int index) { closeTab(index); });
        
        sidebar->setOnItemClick([this](const std::string& id) {
            std::cout << "Sidebar: " << id << " clicked" << std::endl;
            if (id == "home") {
                navigate("zepra://start");
            } else if (id == "settings") {
                navigate("zepra://settings");
            }
        });
    }
    
    void update() {}
    
    void render() {
        context_.beginFrame();
        
        DisplayList displayList;
        chrome_.paint(displayList);
        
        if (activePage_) {
            Rect area = chrome_.contentArea();
            PaintContext ctx(displayList);
            ctx.pushClip(area);
            
            if (activePage_->url() == "zepra://start" || activePage_->url() == "zepra://newtab") {
                // Gradient background
                int numBands = 50;
                float bandHeight = area.height / numBands;
                
                for (int i = 0; i < numBands; i++) {
                    float t = static_cast<float>(i) / numBands;
                    Color bandColor;
                    
                    if (t < 0.5f) {
                        float localT = t * 2.0f;
                        bandColor.r = 218 + static_cast<uint8_t>((201 - 218) * localT);
                        bandColor.g = 173 + static_cast<uint8_t>((164 - 173) * localT);
                        bandColor.b = 193 + static_cast<uint8_t>((213 - 193) * localT);
                        bandColor.a = 255;
                    } else {
                        float localT = (t - 0.5f) * 2.0f;
                        bandColor.r = 201 + static_cast<uint8_t>((145 - 201) * localT);
                        bandColor.g = 164 + static_cast<uint8_t>((145 - 164) * localT);
                        bandColor.b = 213;
                        bandColor.a = 255;
                    }
                    
                    Rect bandRect = {area.x, area.y + i * bandHeight, area.width, bandHeight + 1};
                    ctx.fillRect(bandRect, bandColor);
                }
                
                // Zepra logo
                float logoSize = 80.0f;
                float logoX = area.x + (area.width - logoSize) / 2;
                float logoY = area.y + area.height * 0.35f - logoSize / 2;
                Color logoColor = {255, 255, 255, 220};
                
                float zWidth = logoSize * 0.7f;
                float zHeight = logoSize * 0.8f;
                float cx = logoX + logoSize / 2;
                float cy = logoY + logoSize / 2;
                
                ctx.fillRect({cx - zWidth/2, cy - zHeight/2, zWidth, 8}, logoColor);
                ctx.fillRect({cx - zWidth/2, cy + zHeight/2 - 8, zWidth, 8}, logoColor);
                
                // AI prompt bar
                float barWidth = 500.0f;
                float barHeight = 50.0f;
                float barX = area.x + (area.width - barWidth) / 2;
                float barY = area.y + area.height * 0.6f;
                
                ctx.fillRect({barX, barY, barWidth, barHeight}, {194, 194, 194, 178});
                
            } else {
                ctx.fillRect(area, {255, 255, 255, 255});
                activePage_->paint(displayList);
            }
            
            ctx.popClip();
        }
        
        if (context_.renderBackend()) {
            context_.renderBackend()->executeDisplayList(displayList);
        }
        context_.endFrame();
    }
    
    void resize(int width, int height) {
        context_.resize(width, height);
        chrome_.resize(width, height);
        if (activePage_) {
            auto area = chrome_.contentArea();
            activePage_->setViewport(area.width, area.height);
        }
    }
    
    void newTab(const std::string& title, const std::string& url) {
        auto page = std::make_unique<Page>();
        page->setViewport(chrome_.contentArea().width, chrome_.contentArea().height);
        page->loadFromURL(url);
        pages_.push_back(std::move(page));
        activePage_ = pages_.back().get();
        int index = chrome_.tabBar()->addTab(title, url);
        chrome_.tabBar()->setActiveTab(index);
    }
    
    void closeTab(int index) {
        if (index < 0 || index >= static_cast<int>(pages_.size())) return;
        
        pages_.erase(pages_.begin() + index);
        chrome_.tabBar()->removeTab(index);
        
        int count = pages_.size();
        if (count == 0) {
            newTab("New Tab", "about:blank");
        } else {
            int newIndex = std::min(index, count - 1);
            chrome_.tabBar()->setActiveTab(newIndex);
        }
    }
    
    void switchTab(int index) {
        if (index < 0 || index >= static_cast<int>(pages_.size())) {
            activePage_ = nullptr;
            return;
        }
        activePage_ = pages_[index].get();
        if (activePage_) chrome_.navigationBar()->setURL(activePage_->url());
    }
    
    void navigate(const std::string& url) {
        if (!activePage_) return;
        
        chrome_.navigationBar()->setLoading(true);
        chrome_.navigationBar()->setURL(url);
        activePage_->loadFromURL(url);
        chrome_.navigationBar()->setLoading(false);
    }
    
    void showContextMenu(int x, int y) {
        contextMenu_.setHasSelection(false);
        contextMenu_.setCanGoBack(false);
        contextMenu_.setCanGoForward(false);
        contextMenu_.setPageUrl(activePage_ ? activePage_->url() : "");
        
        contextMenu_.setOnCopy([this]() {
            std::cout << "Copy action" << std::endl;
        });
        
        contextMenu_.setOnPaste([this]() {
            std::cout << "Paste action" << std::endl;
        });
        
        contextMenu_.setOnReload([this]() {
            if (activePage_) activePage_->loadFromURL(activePage_->url());
        });
        
        contextMenu_.show(x, y);
    }
    
    void zoomIn() {
        zoomLevel_ = std::min(zoomLevel_ + 0.1f, 3.0f);
        std::cout << "Zoom: " << (int)(zoomLevel_ * 100) << "%" << std::endl;
    }
    
    void zoomOut() {
        zoomLevel_ = std::max(zoomLevel_ - 0.1f, 0.25f);
        std::cout << "Zoom: " << (int)(zoomLevel_ * 100) << "%" << std::endl;
    }
    
    NXGFXContext context_;
    BrowserChrome chrome_;
    ContextMenu contextMenu_;
    std::vector<std::unique_ptr<Page>> pages_;
    Page* activePage_ = nullptr;
    float zoomLevel_ = 1.0f;
    bool running_ = false;
    
    // Input state tracking
    float lastMouseX_ = 0, lastMouseY_ = 0;
    bool wasLeftDown_ = false;
    bool wasRightDown_ = false;
    bool lastCtrl_ = false, lastShift_ = false, lastAlt_ = false;
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    std::cout << "🚀 Starting Zepra Browser (NXGFX - No SDL)" << std::endl;
    std::cout << "    Version: " << nx_version() << std::endl;
    
    NXBrowserApp app;
    if (app.init()) {
        app.run();
    }
    
    return 0;
}
