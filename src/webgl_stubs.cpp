/*
 * WebGL Stubs - No SDL/WebGL implementation
 * 
 * These stubs satisfy linker requirements when building without SDL.
 * WebGL functionality is unavailable in this build.
 */

#include <iostream>

namespace Zepra {
namespace Runtime {
    class VM;
}
namespace WebCore {

class WebGLBindings {
public:
    static unsigned int createContext(int, int) {
        std::cout << "[WebGL] Not supported (no SDL)" << std::endl;
        return 0;
    }
    
    static void* createJSContextObject(Runtime::VM*, unsigned int) {
        return nullptr;
    }
    
    static void registerNativeFunctions(Runtime::VM*) {
        // No-op
    }
};

} // namespace WebCore
} // namespace Zepra

// Export C symbols for linker
extern "C" {
    unsigned int _ZN5Zepra7WebCore13WebGLBindings13createContextEii(int, int) { return 0; }
    void* _ZN5Zepra7WebCore13WebGLBindings21createJSContextObjectEPNS_7Runtime2VMEj(void*, unsigned int) { return nullptr; }
    void _ZN5Zepra7WebCore13WebGLBindings23registerNativeFunctionsEPNS_7Runtime2VME(void*) {}
}
