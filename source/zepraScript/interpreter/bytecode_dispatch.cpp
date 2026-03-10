// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — bytecode_dispatch.cpp — Computed-goto dispatch, operand decoding

#include <cstdint>
#include <cassert>
#include <cstring>
#include <vector>
#include <functional>

namespace Zepra::Interpreter {

enum class BCOp : uint8_t {
    Nop = 0,
    LdConst, LdNull, LdUndef, LdTrue, LdFalse, LdInt,
    Mov, Add, Sub, Mul, Div, Mod, Neg, Inc, Dec,
    BAnd, BOr, BXor, BNot, Shl, Shr, UShr,
    Eq, SEq, Lt, Le, Gt, Ge, Not,
    Jmp, JmpT, JmpF,
    Call, Ret, RetU,
    LdProp, StProp, LdElem, StElem,
    NewObj, NewArr,
    LdLocal, StLocal, LdUp, StUp, LdGlobal, StGlobal,
    PushScope, PopScope,
    GetIter, IterNext, IterDone,
    Throw, PushTry, PopTry,
    Typeof, Instanceof, In,
    Spread, Rest,
    Debugger,
    OpCount,
};

struct BCInstr {
    BCOp op;
    uint8_t a, b, c;
    int32_t sbx;
    uint32_t bx;
};

struct DispatchResult {
    uint64_t value;
    bool returned;
    bool threw;
};

// Operand decoder for variable-width bytecode.
class OperandDecoder {
public:
    explicit OperandDecoder(const uint8_t* bytecode, size_t length)
        : code_(bytecode), len_(length), pos_(0) {}

    BCOp readOp() { return static_cast<BCOp>(read8()); }
    uint8_t readU8() { return read8(); }
    uint16_t readU16() { return static_cast<uint16_t>(read8()) | (static_cast<uint16_t>(read8()) << 8); }
    int32_t readS32() {
        int32_t v;
        memcpy(&v, code_ + pos_, 4);
        pos_ += 4;
        return v;
    }
    uint32_t readU32() {
        uint32_t v;
        memcpy(&v, code_ + pos_, 4);
        pos_ += 4;
        return v;
    }

    // Decode a full instruction.
    BCInstr decode() {
        BCInstr instr;
        instr.op = readOp();
        instr.a = readU8();
        instr.b = readU8();
        instr.c = readU8();
        instr.sbx = 0;
        instr.bx = 0;
        return instr;
    }

    // Wide instruction decode (for operands > 255).
    BCInstr decodeWide() {
        BCInstr instr;
        instr.op = readOp();
        instr.a = readU8();
        instr.b = readU8();
        instr.c = readU8();
        instr.sbx = readS32();
        instr.bx = static_cast<uint32_t>(instr.sbx);
        return instr;
    }

    size_t position() const { return pos_; }
    void seek(size_t pos) { pos_ = pos < len_ ? pos : len_; }
    bool atEnd() const { return pos_ >= len_; }

private:
    uint8_t read8() {
        return pos_ < len_ ? code_[pos_++] : 0;
    }

    const uint8_t* code_;
    size_t len_;
    size_t pos_;
};

// Exception handler table.
struct TryBlock {
    size_t tryStart;
    size_t tryEnd;
    size_t catchOffset;
    size_t finallyOffset;
    uint8_t catchReg;    // Register to store caught exception
};

class ExceptionTable {
public:
    void addTryBlock(size_t start, size_t end, size_t catchOff,
                     size_t finallyOff, uint8_t catchReg) {
        blocks_.push_back({start, end, catchOff, finallyOff, catchReg});
    }

    const TryBlock* findHandler(size_t ip) const {
        // Search innermost handler covering this IP.
        for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
            if (ip >= it->tryStart && ip < it->tryEnd) {
                return &(*it);
            }
        }
        return nullptr;
    }

    size_t blockCount() const { return blocks_.size(); }
    void clear() { blocks_.clear(); }

private:
    std::vector<TryBlock> blocks_;
};

// Pre-decoded instruction array for faster dispatch.
class InstructionCache {
public:
    void decode(const uint8_t* bytecode, size_t length) {
        OperandDecoder decoder(bytecode, length);
        instructions_.clear();
        while (!decoder.atEnd()) {
            instructions_.push_back(decoder.decode());
        }
    }

    const BCInstr& at(size_t index) const { return instructions_[index]; }
    size_t size() const { return instructions_.size(); }

private:
    std::vector<BCInstr> instructions_;
};

} // namespace Zepra::Interpreter
