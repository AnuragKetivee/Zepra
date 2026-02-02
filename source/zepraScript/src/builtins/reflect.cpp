/**
 * @file reflect.cpp
 * @brief ES6 Reflect API Implementation
 */

#include "zeprascript/builtins/reflect.hpp"
#include "zeprascript/runtime/object.hpp"
#include "zeprascript/runtime/function.hpp"
#include "zeprascript/runtime/value.hpp"
#include <vector>

namespace Zepra::Builtins {

// Reflect.apply(target, thisArgument, argumentsList)
Runtime::Value ReflectBuiltin::apply(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 3) {
        return Runtime::Value::undefined();
    }
    
    Runtime::Value target = info.argument(0);
    Runtime::Value thisArg = info.argument(1);
    Runtime::Value argsList = info.argument(2);
    
    if (!target.isObject() || !target.asObject()->isCallable()) {
        return Runtime::Value::undefined();
    }
    
    Runtime::Function* fn = dynamic_cast<Runtime::Function*>(target.asObject());
    if (!fn) return Runtime::Value::undefined();
    
    std::vector<Runtime::Value> args;
    if (argsList.isObject()) {
        if (auto* arr = dynamic_cast<Runtime::Array*>(argsList.asObject())) {
            for (size_t i = 0; i < arr->length(); i++) {
                args.push_back(arr->at(i));
            }
        }
    }
    
    Runtime::FunctionCallInfo callInfo(info.context(), thisArg, args);
    return fn->builtinFunction()(callInfo);
}

// Reflect.construct(target, argumentsList, newTarget?)
Runtime::Value ReflectBuiltin::construct(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 2) {
        return Runtime::Value::undefined();
    }
    
    Runtime::Value target = info.argument(0);
    Runtime::Value argsList = info.argument(1);
    
    if (!target.isObject() || !target.asObject()->isCallable()) {
        return Runtime::Value::undefined();
    }
    
    // Create new object with target's prototype
    Runtime::Object* instance = new Runtime::Object();
    
    // Get constructor's prototype
    Runtime::Function* fn = dynamic_cast<Runtime::Function*>(target.asObject());
    if (fn) {
        Runtime::Value prototypeVal = fn->get("prototype");
        if (prototypeVal.isObject()) {
            instance->setPrototype(prototypeVal.asObject());
        }
    }
    
    // Call constructor with new instance as this
    std::vector<Runtime::Value> args;
    if (argsList.isObject()) {
        if (auto* arr = dynamic_cast<Runtime::Array*>(argsList.asObject())) {
            for (size_t i = 0; i < arr->length(); i++) {
                args.push_back(arr->at(i));
            }
        }
    }
    
    Runtime::FunctionCallInfo callInfo(info.context(), Runtime::Value::object(instance), args);
    if (fn) {
        fn->builtinFunction()(callInfo);
    }
    
    return Runtime::Value::object(instance);
}

// Reflect.defineProperty(target, propertyKey, attributes)
Runtime::Value ReflectBuiltin::defineProperty(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 3) {
        return Runtime::Value::boolean(false);
    }
    
    Runtime::Value target = info.argument(0);
    Runtime::Value key = info.argument(1);
    Runtime::Value attrs = info.argument(2);
    
    if (!target.isObject()) {
        return Runtime::Value::boolean(false);
    }
    
    Runtime::Object* obj = target.asObject();
    std::string keyStr = key.toString();
    
    if (attrs.isObject()) {
        Runtime::Object* attrsObj = attrs.asObject();
        Runtime::Value value = attrsObj->get("value");
        obj->set(keyStr, value);
    }
    
    return Runtime::Value::boolean(true);
}

// Reflect.deleteProperty(target, propertyKey)
Runtime::Value ReflectBuiltin::deleteProperty(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 2) {
        return Runtime::Value::boolean(false);
    }
    
    Runtime::Value target = info.argument(0);
    Runtime::Value key = info.argument(1);
    
    if (!target.isObject()) {
        return Runtime::Value::boolean(false);
    }
    
    Runtime::Object* obj = target.asObject();
    return Runtime::Value::boolean(obj->deleteProperty(key.toString()));
}

// Reflect.get(target, propertyKey, receiver?)
Runtime::Value ReflectBuiltin::get(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 2) {
        return Runtime::Value::undefined();
    }
    
    Runtime::Value target = info.argument(0);
    Runtime::Value key = info.argument(1);
    
    if (!target.isObject()) {
        return Runtime::Value::undefined();
    }
    
    return target.asObject()->get(key.toString());
}

// Reflect.getOwnPropertyDescriptor(target, propertyKey)
Runtime::Value ReflectBuiltin::getOwnPropertyDescriptor(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 2) {
        return Runtime::Value::undefined();
    }
    
    Runtime::Value target = info.argument(0);
    Runtime::Value key = info.argument(1);
    
    if (!target.isObject()) {
        return Runtime::Value::undefined();
    }
    
    Runtime::Object* obj = target.asObject();
    Runtime::Value value = obj->get(key.toString());
    
    if (value.isUndefined()) {
        return Runtime::Value::undefined();
    }
    
    // Create descriptor object
    Runtime::Object* descriptor = new Runtime::Object();
    descriptor->set("value", value);
    descriptor->set("writable", Runtime::Value::boolean(true));
    descriptor->set("enumerable", Runtime::Value::boolean(true));
    descriptor->set("configurable", Runtime::Value::boolean(true));
    
    return Runtime::Value::object(descriptor);
}

// Reflect.getPrototypeOf(target)
Runtime::Value ReflectBuiltin::getPrototypeOf(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 1) {
        return Runtime::Value::null();
    }
    
    Runtime::Value target = info.argument(0);
    if (!target.isObject()) {
        return Runtime::Value::null();
    }
    
    Runtime::Object* proto = target.asObject()->prototype();
    return proto ? Runtime::Value::object(proto) : Runtime::Value::null();
}

// Reflect.has(target, propertyKey)
Runtime::Value ReflectBuiltin::has(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 2) {
        return Runtime::Value::boolean(false);
    }
    
    Runtime::Value target = info.argument(0);
    Runtime::Value key = info.argument(1);
    
    if (!target.isObject()) {
        return Runtime::Value::boolean(false);
    }
    
    return Runtime::Value::boolean(target.asObject()->has(key.toString()));
}

// Reflect.isExtensible(target)
Runtime::Value ReflectBuiltin::isExtensible(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 1 || !info.argument(0).isObject()) {
        return Runtime::Value::boolean(false);
    }
    
    Runtime::Object* obj = info.argument(0).asObject();
    return Runtime::Value::boolean(obj->isExtensible());
}

// Reflect.ownKeys(target)
Runtime::Value ReflectBuiltin::ownKeys(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 1 || !info.argument(0).isObject()) {
        return Runtime::Value::object(new Runtime::Array({}));
    }
    
    Runtime::Object* obj = info.argument(0).asObject();
    std::vector<Runtime::Value> keys;
    
    for (const auto& key : obj->ownKeys()) {
        keys.push_back(Runtime::Value::string(new Runtime::String(key)));
    }
    
    return Runtime::Value::object(new Runtime::Array(std::move(keys)));
}

// Reflect.preventExtensions(target)
Runtime::Value ReflectBuiltin::preventExtensions(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 1 || !info.argument(0).isObject()) {
        return Runtime::Value::boolean(false);
    }
    
    Runtime::Object* obj = info.argument(0).asObject();
    obj->preventExtensions();
    return Runtime::Value::boolean(true);
}

// Reflect.set(target, propertyKey, value, receiver?)
Runtime::Value ReflectBuiltin::set(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 3) {
        return Runtime::Value::boolean(false);
    }
    
    Runtime::Value target = info.argument(0);
    Runtime::Value key = info.argument(1);
    Runtime::Value value = info.argument(2);
    
    if (!target.isObject()) {
        return Runtime::Value::boolean(false);
    }
    
    target.asObject()->set(key.toString(), value);
    return Runtime::Value::boolean(true);
}

// Reflect.setPrototypeOf(target, prototype)
Runtime::Value ReflectBuiltin::setPrototypeOf(const Runtime::FunctionCallInfo& info) {
    if (info.argumentCount() < 2) {
        return Runtime::Value::boolean(false);
    }
    
    Runtime::Value target = info.argument(0);
    Runtime::Value proto = info.argument(1);
    
    if (!target.isObject()) {
        return Runtime::Value::boolean(false);
    }
    
    Runtime::Object* protoObj = proto.isObject() ? proto.asObject() : nullptr;
    target.asObject()->setPrototype(protoObj);
    return Runtime::Value::boolean(true);
}

// Create Reflect object
Runtime::Object* ReflectBuiltin::createReflectObject(Runtime::Context*) {
    Runtime::Object* reflect = new Runtime::Object();
    
    reflect->set("apply", Runtime::Value::object(new Runtime::Function("apply", apply, 3)));
    reflect->set("construct", Runtime::Value::object(new Runtime::Function("construct", construct, 2)));
    reflect->set("defineProperty", Runtime::Value::object(new Runtime::Function("defineProperty", defineProperty, 3)));
    reflect->set("deleteProperty", Runtime::Value::object(new Runtime::Function("deleteProperty", deleteProperty, 2)));
    reflect->set("get", Runtime::Value::object(new Runtime::Function("get", get, 2)));
    reflect->set("getOwnPropertyDescriptor", Runtime::Value::object(new Runtime::Function("getOwnPropertyDescriptor", getOwnPropertyDescriptor, 2)));
    reflect->set("getPrototypeOf", Runtime::Value::object(new Runtime::Function("getPrototypeOf", getPrototypeOf, 1)));
    reflect->set("has", Runtime::Value::object(new Runtime::Function("has", has, 2)));
    reflect->set("isExtensible", Runtime::Value::object(new Runtime::Function("isExtensible", isExtensible, 1)));
    reflect->set("ownKeys", Runtime::Value::object(new Runtime::Function("ownKeys", ownKeys, 1)));
    reflect->set("preventExtensions", Runtime::Value::object(new Runtime::Function("preventExtensions", preventExtensions, 1)));
    reflect->set("set", Runtime::Value::object(new Runtime::Function("set", set, 3)));
    reflect->set("setPrototypeOf", Runtime::Value::object(new Runtime::Function("setPrototypeOf", setPrototypeOf, 2)));
    
    return reflect;
}

} // namespace Zepra::Builtins
