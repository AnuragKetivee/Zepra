/**
 * @file proxy.cpp
 * @brief JavaScript Proxy and Reflect implementation
 */

#include "zeprascript/runtime/proxy.hpp"
#include "zeprascript/runtime/function.hpp"

namespace Zepra::Runtime {

// =============================================================================
// Proxy Implementation
// =============================================================================

Proxy::Proxy(Object* target, ProxyHandler handler)
    : Object(ObjectType::Ordinary)
    , target_(target)
    , handler_(std::move(handler)) {}

Value Proxy::get(const std::string& key) const {
    if (revoked_) return Value::undefined();
    
    if (handler_.get) {
        return handler_.get(target_, key, const_cast<Proxy*>(this));
    }
    return target_ ? target_->get(key) : Value::undefined();
}

bool Proxy::set(const std::string& key, Value value) {
    if (revoked_) return false;
    
    if (handler_.set) {
        return handler_.set(target_, key, value, this);
    } else if (target_) {
        target_->set(key, value);
    }
    return true;
}

bool Proxy::hasProperty(const std::string& key) const {
    if (revoked_) return false;
    
    if (handler_.has) {
        return handler_.has(target_, key);
    }
    // Default: check target
    return target_ ? target_->get(key).isUndefined() == false : false;
}

bool Proxy::deleteProperty(const std::string& key) {
    if (revoked_) return false;
    
    if (handler_.deleteProperty) {
        return handler_.deleteProperty(target_, key);
    }
    // Default: no-op
    return true;
}

/**
 * @brief Create a Proxy with handler object traps
 * 
 * Extracts trap functions from the handler object and stores them
 * in the ProxyHandler struct for efficient trap invocation.
 */
Proxy* Proxy::create(Object* target, Object* handlerObj) {
    ProxyHandler handler;
    
    if (handlerObj) {
        // Extract 'get' trap
        Value getVal = handlerObj->get("get");
        if (getVal.isObject()) {
            Function* getFn = dynamic_cast<Function*>(getVal.asObject());
            if (getFn) {
                handler.get = [getFn](Object* t, const std::string& k, Object* r) {
                    std::vector<Value> args = {
                        Value::object(t),
                        Value::string(new String(k)),
                        Value::object(r)
                    };
                    FunctionCallInfo info(nullptr, Value::undefined(), args);
                    return getFn->builtinFunction() ? getFn->builtinFunction()(info) 
                                                     : Value::undefined();
                };
            }
        }
        
        // Extract 'set' trap
        Value setVal = handlerObj->get("set");
        if (setVal.isObject()) {
            Function* setFn = dynamic_cast<Function*>(setVal.asObject());
            if (setFn) {
                handler.set = [setFn](Object* t, const std::string& k, Value v, Object* r) {
                    std::vector<Value> args = {
                        Value::object(t),
                        Value::string(new String(k)),
                        v,
                        Value::object(r)
                    };
                    FunctionCallInfo info(nullptr, Value::undefined(), args);
                    Value result = setFn->builtinFunction() ? setFn->builtinFunction()(info) 
                                                            : Value::boolean(true);
                    return result.toBoolean();
                };
            }
        }
        
        // Extract 'has' trap
        Value hasVal = handlerObj->get("has");
        if (hasVal.isObject()) {
            Function* hasFn = dynamic_cast<Function*>(hasVal.asObject());
            if (hasFn) {
                handler.has = [hasFn](Object* t, const std::string& k) {
                    std::vector<Value> args = {
                        Value::object(t),
                        Value::string(new String(k))
                    };
                    FunctionCallInfo info(nullptr, Value::undefined(), args);
                    Value result = hasFn->builtinFunction() ? hasFn->builtinFunction()(info) 
                                                            : Value::boolean(false);
                    return result.toBoolean();
                };
            }
        }
        
        // Extract 'deleteProperty' trap
        Value delVal = handlerObj->get("deleteProperty");
        if (delVal.isObject()) {
            Function* delFn = dynamic_cast<Function*>(delVal.asObject());
            if (delFn) {
                handler.deleteProperty = [delFn](Object* t, const std::string& k) {
                    std::vector<Value> args = {
                        Value::object(t),
                        Value::string(new String(k))
                    };
                    FunctionCallInfo info(nullptr, Value::undefined(), args);
                    Value result = delFn->builtinFunction() ? delFn->builtinFunction()(info) 
                                                            : Value::boolean(true);
                    return result.toBoolean();
                };
            }
        }
    }
    
    return new Proxy(target, handler);
}

std::pair<Proxy*, std::function<void()>> Proxy::createRevocable(Object* target, Object* handlerObj) {
    Proxy* proxy = create(target, handlerObj);
    auto revoker = [proxy]() { proxy->revoke(); };
    return {proxy, revoker};
}

// =============================================================================
// Reflect Implementation
// =============================================================================

Value Reflect::get(Object* target, const std::string& key, Object*) {
    return target ? target->get(key) : Value::undefined();
}

bool Reflect::set(Object* target, const std::string& key, Value value, Object*) {
    if (target) {
        target->set(key, value);
        return true;
    }
    return false;
}

bool Reflect::has(Object* target, const std::string& key) {
    return target && !target->get(key).isUndefined();
}

bool Reflect::deleteProperty(Object* target, const std::string& key) {
    if (!target) return false;
    // Delete the property from the object
    // Returns true if deletion was successful or property didn't exist
    return target->deleteProperty(key);
}

std::vector<std::string> Reflect::ownKeys(Object* target) {
    return target ? target->keys() : std::vector<std::string>{};
}

Value Reflect::apply(Object* target, Value thisArg, const std::vector<Value>& args) {
    Function* fn = dynamic_cast<Function*>(target);
    if (!fn) return Value::undefined();
    
    // Call the function with the provided thisArg and arguments
    return fn->call(nullptr, thisArg, args);
}

Value Reflect::construct(Object* target, const std::vector<Value>& args) {
    Function* fn = dynamic_cast<Function*>(target);
    if (!fn) return Value::undefined();
    
    // Construct the object using the function as constructor
    return fn->construct(nullptr, args);
}

Value Reflect::getOwnPropertyDescriptor(Object*, const std::string&) {
    // TODO: Return property descriptor
    return Value::undefined();
}

bool Reflect::defineProperty(Object*, const std::string&, Object*) {
    // TODO: Define property
    return true;
}

Object* Reflect::getPrototypeOf(Object* target) {
    return target ? target->prototype() : nullptr;
}

bool Reflect::setPrototypeOf(Object* target, Object* proto) {
    if (target) {
        target->setPrototype(proto);
        return true;
    }
    return false;
}

bool Reflect::isExtensible(Object*) {
    return true; // Default
}

bool Reflect::preventExtensions(Object*) {
    return true; // TODO: Mark object as non-extensible
}

// =============================================================================
// ReflectBuiltin Implementation
// =============================================================================

Value ReflectBuiltin::get(Context*, const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isObject() || !args[1].isString()) {
        return Value::undefined();
    }
    
    std::string key = static_cast<String*>(args[1].asObject())->value();
    return Reflect::get(args[0].asObject(), key);
}

Value ReflectBuiltin::set(Context*, const std::vector<Value>& args) {
    if (args.size() < 3 || !args[0].isObject() || !args[1].isString()) {
        return Value::boolean(false);
    }
    
    std::string key = static_cast<String*>(args[1].asObject())->value();
    return Value::boolean(Reflect::set(args[0].asObject(), key, args[2]));
}

Value ReflectBuiltin::has(Context*, const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isObject() || !args[1].isString()) {
        return Value::boolean(false);
    }
    
    std::string key = static_cast<String*>(args[1].asObject())->value();
    return Value::boolean(Reflect::has(args[0].asObject(), key));
}

Value ReflectBuiltin::deleteProperty(Context*, const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isObject() || !args[1].isString()) {
        return Value::boolean(false);
    }
    
    std::string key = static_cast<String*>(args[1].asObject())->value();
    return Value::boolean(Reflect::deleteProperty(args[0].asObject(), key));
}

Value ReflectBuiltin::ownKeys(Context*, const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObject()) {
        return Value::undefined();
    }
    
    std::vector<std::string> keys = Reflect::ownKeys(args[0].asObject());
    Array* result = new Array();
    for (const auto& key : keys) {
        result->push(Value::string(new String(key)));
    }
    return Value::object(result);
}

Value ReflectBuiltin::apply(Context*, const std::vector<Value>& args) {
    if (args.size() < 3 || !args[0].isObject()) {
        return Value::undefined();
    }
    
    std::vector<Value> fnArgs;
    // TODO: Extract args from args[2] array
    return Reflect::apply(args[0].asObject(), args[1], fnArgs);
}

Value ReflectBuiltin::construct(Context*, const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isObject()) {
        return Value::undefined();
    }
    
    std::vector<Value> ctorArgs;
    // TODO: Extract args from args[1] array
    return Reflect::construct(args[0].asObject(), ctorArgs);
}

} // namespace Zepra::Runtime
