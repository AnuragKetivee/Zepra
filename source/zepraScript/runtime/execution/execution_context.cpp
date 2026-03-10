// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — execution_context.cpp — Lexical environment, scope chain, this binding

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace Zepra::Runtime {

union ValueBits {
    double d;
    uint64_t bits;
    void* ptr;
};

enum class BindingKind : uint8_t {
    Var,
    Let,
    Const,
    FunctionDecl,
    ClassDecl,
    CatchParam,
    ImportBinding,
};

struct Binding {
    ValueBits value;
    BindingKind kind;
    bool initialized;        // TDZ check for let/const
    bool mutable_;           // false for const
    bool deletable;          // true for global var without strict

    Binding() : value{}, kind(BindingKind::Var), initialized(false)
        , mutable_(true), deletable(false) {}
};

// Declarative environment record (lexical scoping).
class Environment {
public:
    explicit Environment(Environment* outer = nullptr)
        : outer_(outer), isStrict_(false), isModule_(false), isWith_(false) {}

    // Create a binding (hoisting for var, TDZ entry for let/const).
    bool createBinding(const std::string& name, BindingKind kind) {
        if (bindings_.count(name)) return false;
        Binding b;
        b.kind = kind;
        b.initialized = (kind == BindingKind::Var || kind == BindingKind::FunctionDecl);
        b.mutable_ = (kind != BindingKind::Const);
        bindings_[name] = b;
        return true;
    }

    // Initialize a binding (exits TDZ).
    bool initializeBinding(const std::string& name, ValueBits value) {
        auto it = bindings_.find(name);
        if (it == bindings_.end()) return false;
        it->second.value = value;
        it->second.initialized = true;
        return true;
    }

    // Set a binding value.
    bool setBinding(const std::string& name, ValueBits value) {
        auto it = bindings_.find(name);
        if (it != bindings_.end()) {
            if (!it->second.initialized) return false;  // TDZ
            if (!it->second.mutable_) return false;     // const
            it->second.value = value;
            return true;
        }
        // Walk scope chain.
        if (outer_) return outer_->setBinding(name, value);
        return false;
    }

    // Get a binding value.
    bool getBinding(const std::string& name, ValueBits& out) const {
        auto it = bindings_.find(name);
        if (it != bindings_.end()) {
            if (!it->second.initialized) return false;  // TDZ
            out = it->second.value;
            return true;
        }
        if (outer_) return outer_->getBinding(name, out);
        return false;
    }

    // Check TDZ.
    bool isInitialized(const std::string& name) const {
        auto it = bindings_.find(name);
        if (it != bindings_.end()) return it->second.initialized;
        if (outer_) return outer_->isInitialized(name);
        return false;
    }

    bool hasBinding(const std::string& name) const {
        if (bindings_.count(name)) return true;
        if (outer_) return outer_->hasBinding(name);
        return false;
    }

    bool deleteBinding(const std::string& name) {
        auto it = bindings_.find(name);
        if (it != bindings_.end()) {
            if (!it->second.deletable) return false;
            bindings_.erase(it);
            return true;
        }
        return false;
    }

    Environment* outer() const { return outer_; }
    void setStrict(bool s) { isStrict_ = s; }
    bool isStrict() const { return isStrict_; }
    void setModule(bool m) { isModule_ = m; }
    bool isModule() const { return isModule_; }
    void setWith(bool w) { isWith_ = w; }
    bool isWith() const { return isWith_; }

    size_t bindingCount() const { return bindings_.size(); }

    // Walk all bindings (for debugger).
    template<typename Fn>
    void forEachBinding(Fn&& fn) const {
        for (auto& [name, binding] : bindings_) {
            fn(name, binding);
        }
    }

    // Scope chain depth.
    size_t depth() const {
        size_t d = 0;
        const Environment* env = this;
        while (env) { d++; env = env->outer_; }
        return d;
    }

private:
    Environment* outer_;
    std::unordered_map<std::string, Binding> bindings_;
    bool isStrict_;
    bool isModule_;
    bool isWith_;
};

// Execution context: associates environment, this, realm.
struct ExecutionContext {
    Environment* lexicalEnv;     // Current lexical scope
    Environment* variableEnv;    // var-scoped environment
    ValueBits thisBinding;
    void* realm;                 // Associated realm
    void* function;              // Running function (if any)
    void* scriptOrModule;
    bool isStrict;

    ExecutionContext() : lexicalEnv(nullptr), variableEnv(nullptr)
        , thisBinding{}, realm(nullptr), function(nullptr)
        , scriptOrModule(nullptr), isStrict(false) {}
};

class ExecutionContextStack {
public:
    void push(const ExecutionContext& ctx) {
        stack_.push_back(ctx);
    }

    void pop() {
        if (!stack_.empty()) stack_.pop_back();
    }

    ExecutionContext* running() {
        return stack_.empty() ? nullptr : &stack_.back();
    }

    const ExecutionContext* running() const {
        return stack_.empty() ? nullptr : &stack_.back();
    }

    size_t depth() const { return stack_.size(); }
    bool empty() const { return stack_.empty(); }

    // Suspend/resume for generators and async.
    ExecutionContext suspend() {
        ExecutionContext ctx = stack_.back();
        stack_.pop_back();
        return ctx;
    }

    void resume(const ExecutionContext& ctx) {
        stack_.push_back(ctx);
    }

private:
    std::vector<ExecutionContext> stack_;
};

} // namespace Zepra::Runtime
