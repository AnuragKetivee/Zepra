// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

namespace Zepra::Browser { class Document; }

namespace Zepra::Runtime {

class VM;

class Context {
public:
    explicit Context(VM* vm) : vm_(vm) {}

    VM* vm() const { return vm_; }

    // Browser integration — returns the active Document for this context
    Browser::Document* getDocument() const { return document_; }
    void setDocument(Browser::Document* doc) { document_ = doc; }

private:
    VM* vm_;
    Browser::Document* document_ = nullptr;
};

} // namespace Zepra::Runtime
