// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file isolate.cpp
 * @brief Isolate implementation — isolated JavaScript execution environment
 */

#include "zepra_api.hpp"
#include "runtime/execution/vm.hpp"
#include <memory>
#include <algorithm>

namespace Zepra {

// Global initialization state
static bool g_initialized = false;
static const char* g_version = "1.0.0";

bool initialize() {
    if (g_initialized) return true;
    g_initialized = true;
    return true;
}

void shutdown() {
    g_initialized = false;
}

const char* getVersion() {
    return g_version;
}

// Forward — defined in context.cpp
class ContextImpl;

/**
 * @brief Concrete Isolate implementation
 */
class IsolateImpl : public Isolate {
public:
    explicit IsolateImpl(const IsolateOptions& options)
        : options_(options)
        , objectCount_(0)
        , usedHeapSize_(0)
        , totalHeapSize_(options.initialHeapSize)
    {
    }

    ~IsolateImpl() override {
        contexts_.clear();
    }

    std::unique_ptr<Context> createContext() override;

    void collectGarbage(bool fullGC) override {
        // GC integration point — when a GC heap is wired,
        // this will trigger collection on it.
        (void)fullGC;
    }

    HeapStats getHeapStats() const override {
        return { totalHeapSize_, usedHeapSize_, objectCount_ };
    }

    void registerContext(Context* ctx) {
        contexts_.push_back(ctx);
    }

    void unregisterContext(Context* ctx) {
        contexts_.erase(
            std::remove(contexts_.begin(), contexts_.end(), ctx),
            contexts_.end());
    }

    const IsolateOptions& options() const { return options_; }

    void addObject() { objectCount_++; }
    void removeObject() { if (objectCount_ > 0) objectCount_--; }
    void addHeapUsage(size_t bytes) { usedHeapSize_ += bytes; }

private:
    IsolateOptions options_;
    std::vector<Context*> contexts_;
    size_t totalHeapSize_;
    size_t usedHeapSize_;
    size_t objectCount_;
};

std::unique_ptr<Isolate> Isolate::create(const IsolateOptions& options) {
    if (!g_initialized) {
        initialize();
    }
    return std::make_unique<IsolateImpl>(options);
}

// Exception implementation
Exception::Exception(ErrorType type, std::string message, std::string stack)
    : type_(type)
    , message_(std::move(message))
    , stack_(std::move(stack))
{
}

std::string Exception::toString() const {
    const char* typeName = "Error";
    switch (type_) {
        case ErrorType::SyntaxError: typeName = "SyntaxError"; break;
        case ErrorType::TypeError: typeName = "TypeError"; break;
        case ErrorType::ReferenceError: typeName = "ReferenceError"; break;
        case ErrorType::RangeError: typeName = "RangeError"; break;
        case ErrorType::EvalError: typeName = "EvalError"; break;
        case ErrorType::URIError: typeName = "URIError"; break;
        case ErrorType::InternalError: typeName = "InternalError"; break;
    }

    std::string result = std::string(typeName) + ": " + message_;
    if (!stack_.empty()) {
        result += "\n" + stack_;
    }
    return result;
}

} // namespace Zepra

// Include context implementation
#include "api/context_impl.hpp"

namespace Zepra {

std::unique_ptr<Context> IsolateImpl::createContext() {
    auto ctx = std::make_unique<ContextImpl>(this);
    registerContext(ctx.get());
    return ctx;
}

Isolate* ContextImpl::isolate() {
    return static_cast<Isolate*>(isolate_);
}

} // namespace Zepra
