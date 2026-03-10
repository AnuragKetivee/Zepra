// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — jit_ir.cpp — SSA intermediate representation for optimizing JIT

#include <cstdint>
#include <cassert>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <functional>

namespace Zepra::JIT {

using NodeId = uint32_t;
using BlockId = uint32_t;

static constexpr NodeId kInvalidNode = UINT32_MAX;
static constexpr BlockId kInvalidBlock = UINT32_MAX;

enum class IROp : uint16_t {
    // Constants.
    Constant,        // Load a constant
    Undefined,
    Null,
    Parameter,       // Function parameter

    // Arithmetic.
    AddInt32,
    SubInt32,
    MulInt32,
    DivInt32,
    ModInt32,
    AddFloat64,
    SubFloat64,
    MulFloat64,
    DivFloat64,
    NegFloat64,

    // Bitwise.
    BitAnd32,
    BitOr32,
    BitXor32,
    ShiftLeft32,
    ShiftRight32,
    ShiftRightUnsigned32,

    // Comparison.
    Equal,
    StrictEqual,
    LessThan,
    LessEqual,
    GreaterThan,
    GreaterEqual,

    // Type checks.
    IsNumber,
    IsString,
    IsObject,
    IsNull,
    IsUndefined,
    IsBool,
    TypeGuard,        // Assert type for speculative optimization

    // Conversions.
    Int32ToFloat64,
    Float64ToInt32,
    ToNumber,
    ToString,
    ToBoolean,
    Unbox,           // NaN-box → raw type
    Box,             // Raw type → NaN-box

    // Property access.
    LoadProperty,
    StoreProperty,
    LoadElement,
    StoreElement,
    HasProperty,
    DeleteProperty,
    LoadGlobal,
    StoreGlobal,

    // Object.
    Allocate,
    AllocateArray,
    LoadField,       // Direct object field (known offset)
    StoreField,

    // Control flow.
    Phi,             // SSA phi function
    Branch,          // Conditional branch
    Jump,            // Unconditional jump
    Return,
    Throw,
    Deoptimize,      // Bail out to interpreter

    // Call.
    Call,
    CallNative,
    TailCall,

    // GC.
    WriteBarrier,
    CheckGC,         // Safepoint poll

    // Misc.
    Checkpoint,      // Deoptimization metadata
    FrameState,      // Captures register/stack state
    Unreachable,
};

struct IRNode {
    IROp op;
    NodeId id;
    BlockId block;

    // Inputs (SSA use-def).
    NodeId inputs[4];
    uint8_t inputCount;

    // Output type.
    enum class Type : uint8_t {
        None, Int32, Float64, Bool, Object, String, Any, Tagged,
    };
    Type outputType;

    // Constant value (for Constant nodes).
    union {
        double floatVal;
        int64_t intVal;
        const char* strVal;
    } constant;

    // Metadata.
    uint32_t sourceOffset;
    uint16_t propertyNameId;
    bool hasEffect;         // Has side effect
    bool isGuard;           // Can deoptimize

    // Use chain.
    std::vector<NodeId> uses;

    IRNode() : op(IROp::Constant), id(0), block(0), inputCount(0)
        , outputType(Type::None), constant{}, sourceOffset(0)
        , propertyNameId(0), hasEffect(false), isGuard(false) {}
};

struct BasicBlock {
    BlockId id;
    std::vector<NodeId> nodes;
    std::vector<BlockId> predecessors;
    std::vector<BlockId> successors;
    BlockId dominator;          // Immediate dominator
    uint32_t loopDepth;

    BasicBlock() : id(0), dominator(kInvalidBlock), loopDepth(0) {}
};

class IRGraph {
public:
    IRGraph() : nextNodeId_(0), nextBlockId_(0), entryBlock_(kInvalidBlock) {}

    // Create a basic block.
    BlockId createBlock() {
        BlockId id = nextBlockId_++;
        blocks_.push_back({});
        blocks_.back().id = id;
        return id;
    }

    // Create an IR node.
    NodeId createNode(IROp op, IRNode::Type type, BlockId block,
                      NodeId* inputs = nullptr, uint8_t inputCount = 0) {
        NodeId id = nextNodeId_++;
        nodes_.push_back({});
        IRNode& node = nodes_.back();
        node.op = op;
        node.id = id;
        node.block = block;
        node.outputType = type;
        node.inputCount = inputCount;
        for (uint8_t i = 0; i < inputCount && i < 4; i++) {
            node.inputs[i] = inputs[i];
            if (inputs[i] < nodes_.size()) {
                nodes_[inputs[i]].uses.push_back(id);
            }
        }
        blocks_[block].nodes.push_back(id);
        return id;
    }

    // Create a constant node.
    NodeId createConstInt(BlockId block, int64_t val) {
        NodeId id = createNode(IROp::Constant, IRNode::Type::Int32, block);
        nodes_[id].constant.intVal = val;
        return id;
    }

    NodeId createConstFloat(BlockId block, double val) {
        NodeId id = createNode(IROp::Constant, IRNode::Type::Float64, block);
        nodes_[id].constant.floatVal = val;
        return id;
    }

    // Create a phi node.
    NodeId createPhi(BlockId block, IRNode::Type type) {
        return createNode(IROp::Phi, type, block);
    }

    void addPhiInput(NodeId phi, NodeId value) {
        assert(phi < nodes_.size());
        IRNode& node = nodes_[phi];
        if (node.inputCount < 4) {
            node.inputs[node.inputCount++] = value;
            if (value < nodes_.size()) {
                nodes_[value].uses.push_back(phi);
            }
        }
    }

    // Add edge between blocks.
    void addEdge(BlockId from, BlockId to) {
        blocks_[from].successors.push_back(to);
        blocks_[to].predecessors.push_back(from);
    }

    // Accessors.
    IRNode& node(NodeId id) { return nodes_[id]; }
    const IRNode& node(NodeId id) const { return nodes_[id]; }
    BasicBlock& block(BlockId id) { return blocks_[id]; }
    const BasicBlock& block(BlockId id) const { return blocks_[id]; }
    size_t nodeCount() const { return nodes_.size(); }
    size_t blockCount() const { return blocks_.size(); }
    void setEntryBlock(BlockId id) { entryBlock_ = id; }
    BlockId entryBlock() const { return entryBlock_; }

    // Dead code elimination.
    size_t eliminateDeadCode() {
        size_t eliminated = 0;
        for (auto& n : nodes_) {
            if (n.uses.empty() && !n.hasEffect && n.op != IROp::Return
                && n.op != IROp::Branch && n.op != IROp::Jump) {
                n.op = IROp::Unreachable;
                eliminated++;
            }
        }
        return eliminated;
    }

    // Global value numbering.
    size_t globalValueNumbering() {
        std::unordered_map<uint64_t, NodeId> valueMap;
        size_t merged = 0;

        for (auto& n : nodes_) {
            if (n.hasEffect || n.op == IROp::Phi) continue;

            uint64_t hash = static_cast<uint64_t>(n.op) << 48;
            for (uint8_t i = 0; i < n.inputCount; i++) {
                hash ^= static_cast<uint64_t>(n.inputs[i]) << (i * 12);
            }

            auto it = valueMap.find(hash);
            if (it != valueMap.end()) {
                // Replace uses.
                for (NodeId use : n.uses) {
                    IRNode& user = nodes_[use];
                    for (uint8_t i = 0; i < user.inputCount; i++) {
                        if (user.inputs[i] == n.id) user.inputs[i] = it->second;
                    }
                }
                n.op = IROp::Unreachable;
                merged++;
            } else {
                valueMap[hash] = n.id;
            }
        }
        return merged;
    }

    // Dump IR for debugging.
    std::string dump() const {
        std::string result;
        for (auto& b : blocks_) {
            result += "Block " + std::to_string(b.id) + ":\n";
            for (NodeId nid : b.nodes) {
                const IRNode& n = nodes_[nid];
                result += "  %" + std::to_string(n.id) + " = " +
                          opName(n.op) + "\n";
            }
        }
        return result;
    }

private:
    static const char* opName(IROp op) {
        switch (op) {
            case IROp::Constant: return "Constant";
            case IROp::AddInt32: return "AddInt32";
            case IROp::SubInt32: return "SubInt32";
            case IROp::MulInt32: return "MulInt32";
            case IROp::LessThan: return "LessThan";
            case IROp::Phi: return "Phi";
            case IROp::Branch: return "Branch";
            case IROp::Jump: return "Jump";
            case IROp::Return: return "Return";
            case IROp::Deoptimize: return "Deoptimize";
            case IROp::Call: return "Call";
            case IROp::LoadProperty: return "LoadProp";
            case IROp::StoreProperty: return "StoreProp";
            default: return "Unknown";
        }
    }

    std::vector<IRNode> nodes_;
    std::vector<BasicBlock> blocks_;
    NodeId nextNodeId_;
    BlockId nextBlockId_;
    BlockId entryBlock_;
};

} // namespace Zepra::JIT
