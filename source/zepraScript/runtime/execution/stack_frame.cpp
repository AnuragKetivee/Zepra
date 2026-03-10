// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — stack_frame.cpp — Call stack management, frame push/pop, tail calls

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <vector>
#include <functional>
#include <string>

namespace Zepra::Runtime {

enum class FrameKind : uint8_t {
    JSFunction,      // Normal JS function call
    NativeFunction,  // C++ bound function
    Eval,            // eval() call
    Module,          // Module top-level
    GlobalCode,      // Script-level code
    Generator,       // Generator function
    AsyncFunction,   // Async function
    ClassConstructor,
    ArrowFunction,
};

struct StackFrame {
    FrameKind kind;
    void* functionObj;           // Pointer to the function object
    void* scope;                 // Lexical environment
    const uint8_t* bytecode;     // Pointer to bytecode
    size_t bytecodeLength;
    size_t ip;                   // Instruction pointer
    uint8_t baseReg;             // First register for this frame
    uint8_t argCount;
    uint32_t sourceOffset;       // Source position for stack traces
    uint32_t scriptId;           // Script/module ID
    const char* functionName;    // For stack traces
    const char* fileName;
    uint32_t lineNumber;
    bool isTailCall;
    bool isStrictMode;
    bool isConstructor;          // new.target !== undefined

    StackFrame() : kind(FrameKind::JSFunction), functionObj(nullptr), scope(nullptr)
        , bytecode(nullptr), bytecodeLength(0), ip(0), baseReg(0), argCount(0)
        , sourceOffset(0), scriptId(0), functionName(""), fileName(""), lineNumber(0)
        , isTailCall(false), isStrictMode(false), isConstructor(false) {}
};

class CallStack {
public:
    static constexpr size_t kMaxDepth = 1024;
    static constexpr size_t kInitialCapacity = 64;

    CallStack() : depth_(0) {
        frames_.reserve(kInitialCapacity);
    }

    // Push a new frame. Returns false if stack overflow.
    bool pushFrame(const StackFrame& frame) {
        if (depth_ >= kMaxDepth) return false;
        if (depth_ >= frames_.size()) {
            frames_.push_back(frame);
        } else {
            frames_[depth_] = frame;
        }
        depth_++;
        stats_.totalPushes++;
        return true;
    }

    // Pop the top frame.
    bool popFrame() {
        if (depth_ == 0) return false;
        depth_--;
        stats_.totalPops++;
        return true;
    }

    // Pop and replace with new frame (tail call optimization).
    bool tailCallReplace(const StackFrame& frame) {
        if (depth_ == 0) return false;
        frames_[depth_ - 1] = frame;
        frames_[depth_ - 1].isTailCall = true;
        stats_.tailCalls++;
        return true;
    }

    StackFrame* top() {
        return depth_ > 0 ? &frames_[depth_ - 1] : nullptr;
    }

    const StackFrame* top() const {
        return depth_ > 0 ? &frames_[depth_ - 1] : nullptr;
    }

    StackFrame* frameAt(size_t index) {
        return index < depth_ ? &frames_[index] : nullptr;
    }

    const StackFrame* frameAt(size_t index) const {
        return index < depth_ ? &frames_[index] : nullptr;
    }

    size_t depth() const { return depth_; }
    bool isEmpty() const { return depth_ == 0; }
    bool isFull() const { return depth_ >= kMaxDepth; }

    // Generate stack trace string.
    std::string captureStackTrace(size_t maxFrames = 20) const {
        std::string trace;
        size_t count = depth_ < maxFrames ? depth_ : maxFrames;

        for (size_t i = depth_; i > 0 && count > 0; i--, count--) {
            const StackFrame& f = frames_[i - 1];
            trace += "    at ";
            if (f.functionName && f.functionName[0]) {
                trace += f.functionName;
            } else {
                trace += "<anonymous>";
            }
            trace += " (";
            if (f.fileName && f.fileName[0]) {
                trace += f.fileName;
                trace += ":";
                trace += std::to_string(f.lineNumber);
            } else {
                trace += "<unknown>";
            }
            trace += ")\n";
        }

        if (depth_ > maxFrames) {
            trace += "    ... ";
            trace += std::to_string(depth_ - maxFrames);
            trace += " more frames\n";
        }

        return trace;
    }

    // Walk stack for debugger inspection.
    template<typename Fn>
    void walkFrames(Fn&& fn) const {
        for (size_t i = depth_; i > 0; i--) {
            if (!fn(frames_[i - 1], depth_ - i)) break;
        }
    }

    // Find the nearest frame of a given kind.
    const StackFrame* findFrame(FrameKind kind) const {
        for (size_t i = depth_; i > 0; i--) {
            if (frames_[i - 1].kind == kind) return &frames_[i - 1];
        }
        return nullptr;
    }

    // Count frames of a specific kind.
    size_t countFrames(FrameKind kind) const {
        size_t count = 0;
        for (size_t i = 0; i < depth_; i++) {
            if (frames_[i].kind == kind) count++;
        }
        return count;
    }

    // Check if currently in strict mode.
    bool isInStrictMode() const {
        const StackFrame* f = top();
        return f && f->isStrictMode;
    }

    // Check if in constructor.
    bool isInConstructor() const {
        const StackFrame* f = top();
        return f && f->isConstructor;
    }

    void clear() { depth_ = 0; }

    struct Stats {
        uint64_t totalPushes = 0;
        uint64_t totalPops = 0;
        uint64_t tailCalls = 0;
        size_t peakDepth = 0;
    };

    const Stats& stats() const {
        if (depth_ > const_cast<Stats&>(stats_).peakDepth) {
            const_cast<Stats&>(stats_).peakDepth = depth_;
        }
        return stats_;
    }

private:
    std::vector<StackFrame> frames_;
    size_t depth_;
    Stats stats_;
};

} // namespace Zepra::Runtime
