/**
 * @file DOMBindings.h
 * @brief C++ ↔ JavaScript DOM Interop
 * 
 * WebIDL-style bindings for exposing C++ DOM to JS:
 * - DOMClass: Wrap C++ classes
 * - DOMMethod: Expose methods
 * - DOMProperty: Getters/setters
 * - DOMEventTarget: Event dispatching
 */

#pragma once

#include "../core/EmbedderAPI.h"
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>

namespace Zepra::DOM {

// =============================================================================
// DOM Wrapper Base
// =============================================================================

/**
 * @brief Base class for all DOM wrappers
 */
class DOMWrapper {
public:
    virtual ~DOMWrapper() = default;
    
    // Get the wrapped C++ object
    virtual void* GetNativeObject() const = 0;
    
    // Get wrapper for a native object
    template<typename T>
    static DOMWrapper* Wrap(T* native);
    
    // Get native from wrapper
    template<typename T>
    static T* Unwrap(DOMWrapper* wrapper) {
        return static_cast<T*>(wrapper->GetNativeObject());
    }
};

// =============================================================================
// Property Descriptor
// =============================================================================

enum class PropertyFlags : uint8_t {
    None = 0,
    ReadOnly = 1 << 0,
    DontEnum = 1 << 1,
    DontDelete = 1 << 2,
    Accessor = 1 << 3
};

inline PropertyFlags operator|(PropertyFlags a, PropertyFlags b) {
    return static_cast<PropertyFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

struct DOMPropertyDescriptor {
    std::string name;
    PropertyFlags flags = PropertyFlags::None;
    
    // For data properties
    std::function<ZebraValue(void*)> getter;
    std::function<void(void*, const ZebraValue&)> setter;
    
    bool isReadOnly() const {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(PropertyFlags::ReadOnly)) != 0;
    }
};

// =============================================================================
// Method Descriptor
// =============================================================================

struct DOMMethodDescriptor {
    std::string name;
    int length = 0;  // Expected argument count
    
    std::function<ZebraValue(void*, const std::vector<ZebraValue>&)> callback;
};

// =============================================================================
// DOM Class Definition
// =============================================================================

/**
 * @brief Defines a JavaScript class backed by C++
 */
class DOMClassDefinition {
public:
    DOMClassDefinition(const std::string& className)
        : className_(className) {}
    
    const std::string& ClassName() const { return className_; }
    
    // Add property
    template<typename T, typename R>
    DOMClassDefinition& Property(const std::string& name,
                                  R (T::*getter)() const,
                                  void (T::*setter)(R) = nullptr) {
        DOMPropertyDescriptor desc;
        desc.name = name;
        desc.getter = [getter](void* obj) -> ZebraValue {
            T* self = static_cast<T*>(obj);
            return ToJS((self->*getter)());
        };
        
        if (setter) {
            desc.setter = [setter](void* obj, const ZebraValue& val) {
                T* self = static_cast<T*>(obj);
                (self->*setter)(FromJS<R>(val));
            };
        } else {
            desc.flags = PropertyFlags::ReadOnly;
        }
        
        properties_.push_back(std::move(desc));
        return *this;
    }
    
    // Add method
    template<typename T, typename R, typename... Args>
    DOMClassDefinition& Method(const std::string& name,
                                R (T::*method)(Args...)) {
        DOMMethodDescriptor desc;
        desc.name = name;
        desc.length = sizeof...(Args);
        desc.callback = [method](void* obj, const std::vector<ZebraValue>& args) 
            -> ZebraValue {
            T* self = static_cast<T*>(obj);
            return CallMethod(self, method, args, std::index_sequence_for<Args...>{});
        };
        
        methods_.push_back(std::move(desc));
        return *this;
    }
    
    // Add static method
    template<typename R, typename... Args>
    DOMClassDefinition& StaticMethod(const std::string& name,
                                      R (*method)(Args...)) {
        DOMMethodDescriptor desc;
        desc.name = name;
        desc.length = sizeof...(Args);
        desc.callback = [method](void*, const std::vector<ZebraValue>& args) 
            -> ZebraValue {
            return CallStaticMethod(method, args, std::index_sequence_for<Args...>{});
        };
        
        staticMethods_.push_back(std::move(desc));
        return *this;
    }
    
    const std::vector<DOMPropertyDescriptor>& Properties() const { return properties_; }
    const std::vector<DOMMethodDescriptor>& Methods() const { return methods_; }
    const std::vector<DOMMethodDescriptor>& StaticMethods() const { return staticMethods_; }
    
private:
    template<typename R>
    static ZebraValue ToJS(R value);
    
    template<typename R>
    static R FromJS(const ZebraValue& value);
    
    template<typename T, typename R, typename... Args, size_t... Is>
    static ZebraValue CallMethod(T* self, R (T::*method)(Args...),
                                  const std::vector<ZebraValue>& args,
                                  std::index_sequence<Is...>) {
        return ToJS((self->*method)(FromJS<Args>(args[Is])...));
    }
    
    template<typename R, typename... Args, size_t... Is>
    static ZebraValue CallStaticMethod(R (*method)(Args...),
                                        const std::vector<ZebraValue>& args,
                                        std::index_sequence<Is...>) {
        return ToJS(method(FromJS<Args>(args[Is])...));
    }
    
    std::string className_;
    std::vector<DOMPropertyDescriptor> properties_;
    std::vector<DOMMethodDescriptor> methods_;
    std::vector<DOMMethodDescriptor> staticMethods_;
};

// =============================================================================
// DOM Event Target
// =============================================================================

/**
 * @brief Base class for event-dispatching DOM objects
 */
class DOMEventTarget {
public:
    virtual ~DOMEventTarget() = default;
    
    // Add event listener
    void AddEventListener(const std::string& type, 
                          std::function<void(const ZebraValue&)> listener,
                          bool capture = false) {
        auto& list = capture ? captureListeners_[type] : bubbleListeners_[type];
        list.push_back(std::move(listener));
    }
    
    // Remove event listener
    void RemoveEventListener(const std::string& type, bool capture = false) {
        auto& list = capture ? captureListeners_[type] : bubbleListeners_[type];
        list.clear();
    }
    
    // Dispatch event
    bool DispatchEvent(const std::string& type, const ZebraValue& event) {
        // Capture phase
        auto capIt = captureListeners_.find(type);
        if (capIt != captureListeners_.end()) {
            for (auto& listener : capIt->second) {
                listener(event);
            }
        }
        
        // Bubble phase
        auto bubIt = bubbleListeners_.find(type);
        if (bubIt != bubbleListeners_.end()) {
            for (auto& listener : bubIt->second) {
                listener(event);
            }
        }
        
        return true;
    }
    
private:
    std::unordered_map<std::string, 
        std::vector<std::function<void(const ZebraValue&)>>> captureListeners_;
    std::unordered_map<std::string, 
        std::vector<std::function<void(const ZebraValue&)>>> bubbleListeners_;
};

// =============================================================================
// DOM Class Registry
// =============================================================================

/**
 * @brief Global registry of DOM classes
 */
class DOMClassRegistry {
public:
    static DOMClassRegistry& Instance() {
        static DOMClassRegistry registry;
        return registry;
    }
    
    // Register a class
    void Register(std::type_index type, DOMClassDefinition def) {
        definitions_[type] = std::move(def);
    }
    
    // Get definition
    const DOMClassDefinition* Get(std::type_index type) const {
        auto it = definitions_.find(type);
        return it != definitions_.end() ? &it->second : nullptr;
    }
    
    // Install all classes to context
    void InstallToContext(ZebraContext* ctx);
    
private:
    DOMClassRegistry() = default;
    std::unordered_map<std::type_index, DOMClassDefinition> definitions_;
};

// =============================================================================
// Registration Macro
// =============================================================================

#define ZEPRA_REGISTER_DOM_CLASS(CppClass, JsName) \
    static bool _registered_##CppClass = []() { \
        Zepra::DOM::DOMClassRegistry::Instance().Register( \
            std::type_index(typeid(CppClass)), \
            Zepra::DOM::DOMClassDefinition(JsName) \
        ); \
        return true; \
    }()

} // namespace Zepra::DOM
