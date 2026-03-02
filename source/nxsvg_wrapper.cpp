/**
 * NXSVG C Wrapper Implementation
 * Bridges C code (IconLay) to C++ NXSVG library
 */

#include "nxsvg_wrapper.h"
#include "nxsvg.h"

#include <cstring>
#include <cstdlib>

// Internal helpers for string storage
struct StringCache {
    std::vector<std::string> strings;
    
    const char* store(const std::string& s) {
        strings.push_back(s);
        return strings.back().c_str();
    }
    
    void clear() { strings.clear(); }
};

static StringCache g_stringCache;

extern "C" {

nxsvg_loader_t nxsvg_loader_create(void) {
    return new nxsvg::SvgLoader();
}

void nxsvg_loader_destroy(nxsvg_loader_t loader) {
    delete static_cast<nxsvg::SvgLoader*>(loader);
    g_stringCache.clear();
}

int nxsvg_load_file(nxsvg_loader_t loader, const char* name, const char* path) {
    if (!loader || !name || !path) return 0;
    return static_cast<nxsvg::SvgLoader*>(loader)->loadFromFile(name, path) ? 1 : 0;
}

int nxsvg_load_string(nxsvg_loader_t loader, const char* name, const char* svg_data) {
    if (!loader || !name || !svg_data) return 0;
    return static_cast<nxsvg::SvgLoader*>(loader)->loadFromString(name, svg_data) ? 1 : 0;
}

int nxsvg_has(nxsvg_loader_t loader, const char* name) {
    if (!loader || !name) return 0;
    return static_cast<nxsvg::SvgLoader*>(loader)->has(name) ? 1 : 0;
}

// Get image pointer for querying
nxsvg_image_t nxsvg_get_image(nxsvg_loader_t loader, const char* name) {
    if (!loader || !name) return nullptr;
    return static_cast<nxsvg::SvgLoader*>(loader)->getImage(name);
}

void nxsvg_render(nxsvg_loader_t loader, const char* name,
                  float x, float y, float size,
                  uint8_t r, uint8_t g, uint8_t b) {
    if (!loader || !name) return;
    static_cast<nxsvg::SvgLoader*>(loader)->draw(name, x, y, size, r, g, b);
}

void nxsvg_render_group(nxsvg_loader_t loader, const char* name, const char* group_id,
                        float x, float y, float size,
                        uint8_t r, uint8_t g, uint8_t b) {
    if (!loader || !name || !group_id) return;
    static_cast<nxsvg::SvgLoader*>(loader)->drawGroup(name, group_id, x, y, size, r, g, b);
}

size_t nxsvg_group_count(nxsvg_image_t img) {
    if (!img) return 0;
    return static_cast<const nxsvg::SvgImage*>(img)->groupCount();
}

int nxsvg_get_group_info(nxsvg_image_t img, size_t index, nxsvg_group_info_t* info) {
    if (!img || !info) return 0;
    
    const nxsvg::SvgImage* image = static_cast<const nxsvg::SvgImage*>(img);
    const auto& groups = image->getGroups();
    
    if (index >= groups.size()) return 0;
    
    const nxsvg::SvgGroup& g = groups[index];
    
    info->id = g_stringCache.store(g.id);
    info->name = g_stringCache.store(g.name);
    info->class_name = g_stringCache.store(g.className);
    info->opacity = g.opacity;
    info->visible = g.visible ? 1 : 0;
    info->has_transform = g.hasTransform ? 1 : 0;
    
    for (int i = 0; i < 6; i++) {
        info->transform[i] = g.transform[i];
    }
    
    return 1;
}

int nxsvg_get_group_by_id(nxsvg_image_t img, const char* id, nxsvg_group_info_t* info) {
    if (!img || !id || !info) return 0;
    
    const nxsvg::SvgImage* image = static_cast<const nxsvg::SvgImage*>(img);
    const nxsvg::SvgGroup* g = image->getGroup(id);
    
    if (!g) return 0;
    
    info->id = g_stringCache.store(g->id);
    info->name = g_stringCache.store(g->name);
    info->class_name = g_stringCache.store(g->className);
    info->opacity = g->opacity;
    info->visible = g->visible ? 1 : 0;
    info->has_transform = g->hasTransform ? 1 : 0;
    
    for (int i = 0; i < 6; i++) {
        info->transform[i] = g->transform[i];
    }
    
    return 1;
}

void nxsvg_get_viewbox(nxsvg_image_t img, float* x, float* y, float* w, float* h) {
    if (!img) return;
    const nxsvg::SvgImage* image = static_cast<const nxsvg::SvgImage*>(img);
    if (x) *x = image->viewBoxX;
    if (y) *y = image->viewBoxY;
    if (w) *w = image->viewBoxW;
    if (h) *h = image->viewBoxH;
}

size_t nxsvg_shape_count(nxsvg_image_t img) {
    if (!img) return 0;
    return static_cast<const nxsvg::SvgImage*>(img)->shapes.size();
}

} // extern "C"
