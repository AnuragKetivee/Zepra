/**
 * @file nxgfx_context.cpp
 * @brief NXGFX context implementation - SDL replacement
 * 
 * Uses NXGFX (Rust) via C FFI for all rendering.
 * No SDL dependency.
 */

#include "webcore/nxgfx_context.hpp"
#include <iostream>

namespace Zepra::WebCore {

// =============================================================================
// NXGFXContext Implementation
// =============================================================================

NXGFXContext::NXGFXContext() = default;

NXGFXContext::~NXGFXContext() {
    cleanup();
}

bool NXGFXContext::initialize(const std::string& title, int width, int height, 
                               NXRenderMode mode) {
    (void)title;  // TODO: pass to window creation
    width_ = width;
    height_ = height;
    
    std::cout << "NXGFX: Initializing " << width << "x" << height << std::endl;
    std::cout << "NXGFX: Version " << nx_version() << std::endl;
    
    // Detect system capabilities
    NxSystemInfo* sys = nx_detect_system();
    if (sys) {
        std::cout << "NXGFX: GPU: " << sys->gpu_name << " (" << sys->gpu_vendor << ")" << std::endl;
        std::cout << "NXGFX: Display: " << sys->display_width << "x" << sys->display_height 
                  << " @ " << sys->display_refresh << "Hz" << std::endl;
        nx_free_system_info(sys);
    }
    
    // Create GPU context
    gpu_ = nx_gpu_create_with_size(width, height);
    if (!gpu_) {
        std::cerr << "NXGFX: Failed to create GPU context" << std::endl;
        return false;
    }
    
    // Create input state objects
    mouse_ = nx_mouse_create();
    keyboard_ = nx_keyboard_create();
    touch_ = nx_touch_create();
    
    // Create theme (default to dark)
    theme_ = nx_theme_dark();
    
    // Create render backend
    renderBackend_ = std::make_unique<NXGFXRenderBackend>(gpu_);
    
    currentMode_ = (mode == NXRenderMode::Software) ? NXRenderMode::Software : NXRenderMode::GPU;
    
    std::cout << "NXGFX: Context initialized successfully" << std::endl;
    return true;
}

void NXGFXContext::beginFrame() {
    if (!gpu_) return;
    
    // Get background color from theme
    NxColor bg = theme_ ? nx_theme_get_background_color(theme_) 
                        : NxColor{35, 35, 40, 255};
    
    nx_gpu_clear(gpu_, bg);
}

void NXGFXContext::endFrame() {
    if (!gpu_) return;
    nx_gpu_present(gpu_);
}

void NXGFXContext::resize(int width, int height) {
    width_ = width;
    height_ = height;
    if (gpu_) {
        nx_gpu_resize(gpu_, width, height);
    }
    if (renderBackend_) {
        renderBackend_->resize(width, height);
    }
}

void NXGFXContext::getSize(int& width, int& height) const {
    width = width_;
    height = height_;
}

std::unique_ptr<DisplayList> NXGFXContext::createDisplayList() {
    return std::make_unique<DisplayList>();
}

void NXGFXContext::cleanup() {
    renderBackend_.reset();
    
    if (theme_) { nx_theme_destroy(theme_); theme_ = nullptr; }
    if (touch_) { nx_touch_destroy(touch_); touch_ = nullptr; }
    if (keyboard_) { nx_keyboard_destroy(keyboard_); keyboard_ = nullptr; }
    if (mouse_) { nx_mouse_destroy(mouse_); mouse_ = nullptr; }
    if (gpu_) { nx_gpu_destroy(gpu_); gpu_ = nullptr; }
}

bool NXGFXContext::initGPU() {
    // Already done in initialize()
    return gpu_ != nullptr;
}

// =============================================================================
// NXGFXRenderBackend Implementation
// =============================================================================

NXGFXRenderBackend::NXGFXRenderBackend(NxGpuContext* gpu) : gpu_(gpu) {}

void NXGFXRenderBackend::executeDisplayList(const DisplayList& list) {
    if (!gpu_) return;
    
    for (const auto& cmd : list.commands()) {
        switch (cmd.type) {
            case PaintCommandType::FillRect: {
                NxRect rect = {
                    static_cast<float>(cmd.rect.x),
                    static_cast<float>(cmd.rect.y),
                    static_cast<float>(cmd.rect.width),
                    static_cast<float>(cmd.rect.height)
                };
                NxColor color = {cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a};
                nx_gpu_fill_rect(gpu_, rect, color);
                break;
            }
            
            case PaintCommandType::FillRoundedRect: {
                NxRect rect = {
                    static_cast<float>(cmd.rect.x),
                    static_cast<float>(cmd.rect.y),
                    static_cast<float>(cmd.rect.width),
                    static_cast<float>(cmd.rect.height)
                };
                NxColor color = {cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a};
                nx_gpu_fill_rounded_rect(gpu_, rect, color, cmd.radius);
                break;
            }
            
            case PaintCommandType::DrawText: {
                NxColor color = {cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a};
                nx_gpu_draw_text(gpu_, cmd.text.c_str(), 
                                 static_cast<float>(cmd.rect.x),
                                 static_cast<float>(cmd.rect.y),
                                 color);
                break;
            }
            
            case PaintCommandType::FillCircle: {
                NxColor color = {cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a};
                float cx = cmd.rect.x + cmd.rect.width / 2.0f;
                float cy = cmd.rect.y + cmd.rect.height / 2.0f;
                float radius = cmd.rect.width / 2.0f;
                nx_gpu_fill_circle(gpu_, cx, cy, radius, color);
                break;
            }
            
            case PaintCommandType::PushClip:
            case PaintCommandType::PopClip:
                // TODO: Implement clipping in NXGFX
                break;
                
            default:
                break;
        }
    }
}

void NXGFXRenderBackend::present() {
    if (gpu_) {
        nx_gpu_present(gpu_);
    }
}

void NXGFXRenderBackend::resize(int width, int height) {
    width_ = width;
    height_ = height;
    if (gpu_) {
        nx_gpu_resize(gpu_, width, height);
    }
}

void* NXGFXRenderBackend::createTexture(int width, int height) {
    // TODO: Implement texture creation via NXGFX
    (void)width;
    (void)height;
    return nullptr;
}

void NXGFXRenderBackend::destroyTexture(void* texture) {
    // TODO: Implement texture destruction via NXGFX
    (void)texture;
}

} // namespace Zepra::WebCore
