// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — jit_lowering.cpp — IR → MIR lowering, instruction selection

#include <cstdint>
#include <cassert>
#include <vector>
#include <string>
#include <unordered_map>

namespace Zepra::JIT {

enum class MachineOp : uint16_t {
    // x86-64 integer.
    MOV_r_r,       MOV_r_imm,     MOV_r_mem,     MOV_mem_r,
    ADD_r_r,       ADD_r_imm,     SUB_r_r,       SUB_r_imm,
    IMUL_r_r,      IDIV_r,        NEG_r,
    AND_r_r,       OR_r_r,        XOR_r_r,       NOT_r,
    SHL_r_cl,      SHR_r_cl,      SAR_r_cl,
    CMP_r_r,       CMP_r_imm,     TEST_r_r,

    // x86-64 float (SSE2/AVX).
    MOVSD_xmm_xmm, MOVSD_xmm_mem, MOVSD_mem_xmm,
    ADDSD_xmm,     SUBSD_xmm,     MULSD_xmm,    DIVSD_xmm,
    UCOMISD_xmm,   CVTSI2SD_xmm,  CVTTSD2SI_r,

    // Control flow.
    JMP_rel,       JMP_r,
    JCC_rel,       // Conditional jump (cc = condition code)
    CALL_r,        CALL_rel,      RET,

    // Stack.
    PUSH_r,        POP_r,
    LEA_r_mem,

    // NaN-boxing.
    BOX_int,       BOX_double,    BOX_object,
    UNBOX_int,     UNBOX_double,  UNBOX_object,
    TAG_CHECK,     // Check NaN-box tag

    // GC.
    WRITE_BARRIER, SAFEPOINT_POLL,

    // Meta.
    NOP,           LABEL,         COMMENT,
};

enum class ConditionCode : uint8_t {
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    Above, AboveEqual, Below, BelowEqual, // Unsigned
    Overflow, NotOverflow, Sign, NotSign,
};

enum class Register : uint8_t {
    RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
    R8, R9, R10, R11, R12, R13, R14, R15,
    XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
    XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15,
    None,
};

struct Operand {
    enum class Kind : uint8_t { Reg, Imm, Mem, Label };
    Kind kind;
    Register reg;
    int64_t imm;
    Register base;
    Register index;
    uint8_t scale;     // 1, 2, 4, 8
    int32_t disp;      // Displacement
    uint32_t labelId;

    Operand() : kind(Kind::Reg), reg(Register::None), imm(0)
        , base(Register::None), index(Register::None), scale(1)
        , disp(0), labelId(0) {}

    static Operand fromReg(Register r) {
        Operand o; o.kind = Kind::Reg; o.reg = r; return o;
    }
    static Operand fromImm(int64_t v) {
        Operand o; o.kind = Kind::Imm; o.imm = v; return o;
    }
    static Operand fromMem(Register base, int32_t disp = 0) {
        Operand o; o.kind = Kind::Mem; o.base = base; o.disp = disp; return o;
    }
    static Operand fromLabel(uint32_t id) {
        Operand o; o.kind = Kind::Label; o.labelId = id; return o;
    }
};

struct MachineInstr {
    MachineOp op;
    Operand dst;
    Operand src;
    ConditionCode cc;
    uint32_t irNodeId;      // Back-reference to IR node

    MachineInstr() : op(MachineOp::NOP), cc(ConditionCode::Equal), irNodeId(0) {}
};

class IRLowering {
public:
    void lower(const std::vector<uint16_t>& irOps, const std::vector<uint32_t>& irInputs) {
        output_.clear();
        for (size_t i = 0; i < irOps.size(); i++) {
            lowerNode(static_cast<uint16_t>(irOps[i]), i, irInputs);
        }
    }

    const std::vector<MachineInstr>& output() const { return output_; }

    // Instruction selection for common patterns.
    void lowerAdd(uint32_t dst, uint32_t lhs, uint32_t rhs, bool isFloat) {
        MachineInstr instr;
        if (isFloat) {
            emit(MachineOp::MOVSD_xmm_xmm, vreg(dst), vreg(lhs));
            emit(MachineOp::ADDSD_xmm, vreg(dst), vreg(rhs));
        } else {
            emit(MachineOp::MOV_r_r, vreg(dst), vreg(lhs));
            emit(MachineOp::ADD_r_r, vreg(dst), vreg(rhs));
        }
    }

    void lowerSub(uint32_t dst, uint32_t lhs, uint32_t rhs, bool isFloat) {
        if (isFloat) {
            emit(MachineOp::MOVSD_xmm_xmm, vreg(dst), vreg(lhs));
            emit(MachineOp::SUBSD_xmm, vreg(dst), vreg(rhs));
        } else {
            emit(MachineOp::MOV_r_r, vreg(dst), vreg(lhs));
            emit(MachineOp::SUB_r_r, vreg(dst), vreg(rhs));
        }
    }

    void lowerMul(uint32_t dst, uint32_t lhs, uint32_t rhs, bool isFloat) {
        if (isFloat) {
            emit(MachineOp::MOVSD_xmm_xmm, vreg(dst), vreg(lhs));
            emit(MachineOp::MULSD_xmm, vreg(dst), vreg(rhs));
        } else {
            emit(MachineOp::MOV_r_r, vreg(dst), vreg(lhs));
            emit(MachineOp::IMUL_r_r, vreg(dst), vreg(rhs));
        }
    }

    void lowerCompare(uint32_t dst, uint32_t lhs, uint32_t rhs,
                      ConditionCode cc, bool isFloat) {
        if (isFloat) {
            emit(MachineOp::UCOMISD_xmm, vreg(lhs), vreg(rhs));
        } else {
            emit(MachineOp::CMP_r_r, vreg(lhs), vreg(rhs));
        }
        // Set result via conditional move.
        emit(MachineOp::MOV_r_imm, vreg(dst), Operand::fromImm(0));
        MachineInstr setcc;
        setcc.op = MachineOp::JCC_rel;
        setcc.cc = cc;
        output_.push_back(setcc);
    }

    void lowerBranch(uint32_t cond, uint32_t trueLabel, uint32_t falseLabel) {
        emit(MachineOp::TEST_r_r, vreg(cond), vreg(cond));
        MachineInstr jcc;
        jcc.op = MachineOp::JCC_rel;
        jcc.cc = ConditionCode::NotEqual;
        jcc.dst = Operand::fromLabel(trueLabel);
        output_.push_back(jcc);
        emit(MachineOp::JMP_rel, Operand::fromLabel(falseLabel));
    }

    void lowerCall(uint32_t target, uint32_t* args, uint8_t argc) {
        // System V AMD64 ABI: rdi, rsi, rdx, rcx, r8, r9.
        static const Register argRegs[] = {
            Register::RDI, Register::RSI, Register::RDX,
            Register::RCX, Register::R8, Register::R9,
        };
        for (uint8_t i = 0; i < argc && i < 6; i++) {
            emit(MachineOp::MOV_r_r, Operand::fromReg(argRegs[i]), vreg(args[i]));
        }
        // Stack args for argc > 6.
        for (uint8_t i = argc; i > 6; i--) {
            emit(MachineOp::PUSH_r, vreg(args[i - 1]));
        }
        emit(MachineOp::CALL_r, vreg(target));
    }

    void lowerReturn(uint32_t value) {
        emit(MachineOp::MOV_r_r, Operand::fromReg(Register::RAX), vreg(value));
        emit(MachineOp::RET);
    }

    void lowerWriteBarrier(uint32_t obj, uint32_t slot) {
        emit(MachineOp::WRITE_BARRIER, vreg(obj), vreg(slot));
    }

    void lowerSafepointPoll() {
        emit(MachineOp::SAFEPOINT_POLL);
    }

private:
    void lowerNode(uint16_t op, size_t idx, const std::vector<uint32_t>& inputs) {
        // Dispatch to specific lowering based on IR opcode.
        MachineInstr nop;
        nop.op = MachineOp::NOP;
        nop.irNodeId = static_cast<uint32_t>(idx);
        output_.push_back(nop);
    }

    void emit(MachineOp op, Operand dst = {}, Operand src = {}) {
        MachineInstr instr;
        instr.op = op;
        instr.dst = dst;
        instr.src = src;
        output_.push_back(instr);
    }

    Operand vreg(uint32_t id) {
        // Virtual register — will be assigned physical register by regalloc.
        Operand o;
        o.kind = Operand::Kind::Reg;
        o.reg = static_cast<Register>(id & 0x1F);
        return o;
    }

    std::vector<MachineInstr> output_;
};

} // namespace Zepra::JIT
