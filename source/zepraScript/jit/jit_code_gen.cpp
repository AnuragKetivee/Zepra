// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — jit_code_gen.cpp — x86-64 machine code emission

#include <cstdint>
#include <cassert>
#include <cstring>
#include <vector>

namespace Zepra::JIT {

class X64CodeGen {
public:
    X64CodeGen() { buffer_.reserve(4096); }

    // REX prefix generation.
    void emitREX(bool w, bool r, bool x, bool b) {
        uint8_t rex = 0x40;
        if (w) rex |= 0x08;
        if (r) rex |= 0x04;
        if (x) rex |= 0x02;
        if (b) rex |= 0x01;
        emit8(rex);
    }

    // ModR/M byte.
    void emitModRM(uint8_t mod, uint8_t reg, uint8_t rm) {
        emit8((mod << 6) | ((reg & 0x7) << 3) | (rm & 0x7));
    }

    // SIB byte.
    void emitSIB(uint8_t scale, uint8_t index, uint8_t base) {
        emit8((scale << 6) | ((index & 0x7) << 3) | (base & 0x7));
    }

    // MOV reg, reg (64-bit).
    void movRR(uint8_t dst, uint8_t src) {
        emitREX(true, src > 7, false, dst > 7);
        emit8(0x89);
        emitModRM(3, src, dst);
    }

    // MOV reg, imm64.
    void movRI(uint8_t dst, int64_t imm) {
        emitREX(true, false, false, dst > 7);
        emit8(0xB8 + (dst & 0x7));
        emit64(imm);
    }

    // MOV reg, [base + disp32].
    void movRM(uint8_t dst, uint8_t base, int32_t disp) {
        emitREX(true, dst > 7, false, base > 7);
        emit8(0x8B);
        if (disp == 0 && (base & 0x7) != 5) {
            emitModRM(0, dst, base);
        } else if (disp >= -128 && disp <= 127) {
            emitModRM(1, dst, base);
            emit8(static_cast<uint8_t>(disp));
        } else {
            emitModRM(2, dst, base);
            emit32(disp);
        }
        if ((base & 0x7) == 4) emitSIB(0, 4, 4);  // RSP needs SIB
    }

    // MOV [base + disp32], reg.
    void movMR(uint8_t base, int32_t disp, uint8_t src) {
        emitREX(true, src > 7, false, base > 7);
        emit8(0x89);
        if (disp == 0 && (base & 0x7) != 5) {
            emitModRM(0, src, base);
        } else if (disp >= -128 && disp <= 127) {
            emitModRM(1, src, base);
            emit8(static_cast<uint8_t>(disp));
        } else {
            emitModRM(2, src, base);
            emit32(disp);
        }
        if ((base & 0x7) == 4) emitSIB(0, 4, 4);
    }

    // ADD reg, reg.
    void addRR(uint8_t dst, uint8_t src) {
        emitREX(true, src > 7, false, dst > 7);
        emit8(0x01);
        emitModRM(3, src, dst);
    }

    // ADD reg, imm32.
    void addRI(uint8_t dst, int32_t imm) {
        emitREX(true, false, false, dst > 7);
        emit8(0x81);
        emitModRM(3, 0, dst);
        emit32(imm);
    }

    // SUB reg, reg.
    void subRR(uint8_t dst, uint8_t src) {
        emitREX(true, src > 7, false, dst > 7);
        emit8(0x29);
        emitModRM(3, src, dst);
    }

    // IMUL reg, reg.
    void imulRR(uint8_t dst, uint8_t src) {
        emitREX(true, dst > 7, false, src > 7);
        emit8(0x0F); emit8(0xAF);
        emitModRM(3, dst, src);
    }

    // CMP reg, reg.
    void cmpRR(uint8_t lhs, uint8_t rhs) {
        emitREX(true, rhs > 7, false, lhs > 7);
        emit8(0x39);
        emitModRM(3, rhs, lhs);
    }

    // CMP reg, imm32.
    void cmpRI(uint8_t reg, int32_t imm) {
        emitREX(true, false, false, reg > 7);
        emit8(0x81);
        emitModRM(3, 7, reg);
        emit32(imm);
    }

    // TEST reg, reg.
    void testRR(uint8_t r1, uint8_t r2) {
        emitREX(true, r2 > 7, false, r1 > 7);
        emit8(0x85);
        emitModRM(3, r2, r1);
    }

    // JMP rel32.
    void jmpRel(int32_t offset) {
        emit8(0xE9);
        emit32(offset);
    }

    // Jcc rel32 (conditional jump).
    void jccRel(uint8_t cc, int32_t offset) {
        emit8(0x0F);
        emit8(0x80 + cc);
        emit32(offset);
    }

    // CALL rel32.
    void callRel(int32_t offset) {
        emit8(0xE8);
        emit32(offset);
    }

    // CALL reg.
    void callR(uint8_t reg) {
        if (reg > 7) emitREX(false, false, false, true);
        emit8(0xFF);
        emitModRM(3, 2, reg);
    }

    // RET.
    void ret() { emit8(0xC3); }

    // PUSH reg64.
    void pushR(uint8_t reg) {
        if (reg > 7) emitREX(false, false, false, true);
        emit8(0x50 + (reg & 0x7));
    }

    // POP reg64.
    void popR(uint8_t reg) {
        if (reg > 7) emitREX(false, false, false, true);
        emit8(0x58 + (reg & 0x7));
    }

    // LEA reg, [base + disp32].
    void leaRM(uint8_t dst, uint8_t base, int32_t disp) {
        emitREX(true, dst > 7, false, base > 7);
        emit8(0x8D);
        emitModRM(2, dst, base);
        emit32(disp);
    }

    // NOP (1-byte).
    void nop() { emit8(0x90); }

    // Multi-byte NOP for alignment.
    void nopN(size_t n) {
        while (n >= 9) { emit8(0x66); emit8(0x0F); emit8(0x1F);
            emit8(0x84); emit8(0x00); emit32(0); n -= 9; }
        while (n > 0) { nop(); n--; }
    }

    // MOVSD xmm, xmm.
    void movsdXX(uint8_t dst, uint8_t src) {
        emit8(0xF2);
        if (dst > 7 || src > 7) emitREX(false, dst > 7, false, src > 7);
        emit8(0x0F); emit8(0x10);
        emitModRM(3, dst, src);
    }

    // ADDSD xmm, xmm.
    void addsdXX(uint8_t dst, uint8_t src) {
        emit8(0xF2);
        if (dst > 7 || src > 7) emitREX(false, dst > 7, false, src > 7);
        emit8(0x0F); emit8(0x58);
        emitModRM(3, dst, src);
    }

    // SUBSD xmm, xmm.
    void subsdXX(uint8_t dst, uint8_t src) {
        emit8(0xF2);
        if (dst > 7 || src > 7) emitREX(false, dst > 7, false, src > 7);
        emit8(0x0F); emit8(0x5C);
        emitModRM(3, dst, src);
    }

    // MULSD xmm, xmm.
    void mulsdXX(uint8_t dst, uint8_t src) {
        emit8(0xF2);
        if (dst > 7 || src > 7) emitREX(false, dst > 7, false, src > 7);
        emit8(0x0F); emit8(0x59);
        emitModRM(3, dst, src);
    }

    // Relocation support.
    struct Relocation {
        size_t offset;
        uint32_t targetLabelId;
        enum class Kind { Rel32, Abs64 } kind;
    };

    void addRelocation(uint32_t labelId) {
        relocations_.push_back({buffer_.size() - 4, labelId, Relocation::Kind::Rel32});
    }

    // Patch a rel32 at offset.
    void patchRel32(size_t offset, int32_t value) {
        memcpy(buffer_.data() + offset, &value, 4);
    }

    // Buffer access.
    const uint8_t* code() const { return buffer_.data(); }
    size_t codeSize() const { return buffer_.size(); }
    const std::vector<Relocation>& relocations() const { return relocations_; }

    void clear() { buffer_.clear(); relocations_.clear(); }

private:
    void emit8(uint8_t b) { buffer_.push_back(b); }
    void emit32(int32_t v) {
        uint8_t bytes[4];
        memcpy(bytes, &v, 4);
        buffer_.insert(buffer_.end(), bytes, bytes + 4);
    }
    void emit64(int64_t v) {
        uint8_t bytes[8];
        memcpy(bytes, &v, 8);
        buffer_.insert(buffer_.end(), bytes, bytes + 8);
    }

    std::vector<uint8_t> buffer_;
    std::vector<Relocation> relocations_;
};

} // namespace Zepra::JIT
