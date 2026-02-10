#include "codegen_lowering_helpers.h"

#include "codegen_helpers.h"

#include <set>
#include <sstream>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{

std::string valueToExpr(const ir::Value& value, LoweringContext& context)
{
    switch (value.kind)
    {
    case ir::ValueKind::IMMEDIATE:
        return std::to_string(value.immediate);
    case ir::ValueKind::ADDRESS:
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << value.address;
        return stream.str();
    }
    case ir::ValueKind::REGISTER:
        return "context.regs[" + std::to_string(value.reg) + "]";
    case ir::ValueKind::TEMPORARY:
    {
        auto it = context.temporaries.find(value.temporaryId);
        if (it != context.temporaries.end())
        {
            return it->second;
        }
        std::string name = "temp" + std::to_string(value.temporaryId);
        context.temporaries[value.temporaryId] = name;
        return name;
    }
    case ir::ValueKind::SPECIAL:
        return value.specialReg == ir::SpecialRegister::HI ? "context.hi" : "context.lo";
    case ir::ValueKind::INVALID:
        return "/* invalid */ 0";
    }
    return "0";
}

std::string opcodeToComment(ir::Opcode opcode)
{
    switch (opcode)
    {
    case ir::Opcode::NOP:
        return "nop";
    case ir::Opcode::PHI:
        return "phi";
    case ir::Opcode::MOVE:
        return "move";
    case ir::Opcode::ADD:
        return "add";
    case ir::Opcode::SUB:
        return "sub";
    case ir::Opcode::AND:
        return "and";
    case ir::Opcode::OR:
        return "or";
    case ir::Opcode::XOR:
        return "xor";
    case ir::Opcode::SHL:
        return "shl";
    case ir::Opcode::SHR_LOGICAL:
        return "shr_logical";
    case ir::Opcode::SHR_ARITH:
        return "shr_arith";
    case ir::Opcode::MUL:
        return "mul";
    case ir::Opcode::MULU:
        return "mulu";
    case ir::Opcode::DIV:
        return "div";
    case ir::Opcode::DIVU:
        return "divu";
    case ir::Opcode::COMPARE_EQ:
        return "cmp_eq";
    case ir::Opcode::COMPARE_NE:
        return "cmp_ne";
    case ir::Opcode::COMPARE_LT:
        return "cmp_lt";
    case ir::Opcode::COMPARE_LTU:
        return "cmp_ltu";
    case ir::Opcode::COMPARE_LE:
        return "cmp_le";
    case ir::Opcode::COMPARE_GT:
        return "cmp_gt";
    case ir::Opcode::COMPARE_GE:
        return "cmp_ge";
    case ir::Opcode::LOAD:
        return "load";
    case ir::Opcode::STORE:
        return "store";
    case ir::Opcode::MMIO_LOAD:
        return "mmio_load";
    case ir::Opcode::MMIO_STORE:
        return "mmio_store";
    case ir::Opcode::BRANCH:
        return "branch";
    case ir::Opcode::JUMP:
        return "jump";
    case ir::Opcode::CALL:
        return "call";
    case ir::Opcode::SYSCALL:
        return "syscall";
    case ir::Opcode::RETURN:
        return "return";
    }
    return "unknown";
}

std::set<u32> collectTemporaries(const ir::Function& function)
{
    std::set<u32> temporaries;
    for (const auto& block : function.blocks)
    {
        for (const auto& instruction : block.instructions)
        {
            for (const auto& value : instruction.inputs)
            {
                if (value.kind == ir::ValueKind::TEMPORARY)
                {
                    temporaries.insert(value.temporaryId);
                }
            }
            for (const auto& value : instruction.outputs)
            {
                if (value.kind == ir::ValueKind::TEMPORARY)
                {
                    temporaries.insert(value.temporaryId);
                }
            }
        }
    }
    return temporaries;
}

std::string resolveBlockId(std::string_view name,
                           const std::unordered_map<std::string, std::string>& blockNames)
{
    auto it = blockNames.find(std::string(name));
    if (it != blockNames.end())
    {
        return "BlockId::" + it->second;
    }
    return "BlockId::" + toIdentifier(name);
}

std::unordered_map<std::string, size_t> buildBlockIndex(const ir::Function& function)
{
    std::unordered_map<std::string, size_t> indexMap;
    for (size_t index = 0; index < function.blocks.size(); ++index)
    {
        indexMap[function.blocks[index].name] = index;
    }
    return indexMap;
}

std::vector<std::vector<std::string>>
buildPredecessors(const ir::Function& function,
                  const std::unordered_map<std::string, size_t>& indexMap)
{
    std::vector<std::vector<std::string>> predecessors(function.blocks.size());
    for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
    {
        const auto& block = function.blocks[blockIndex];
        for (const auto& successor : block.successors)
        {
            auto it = indexMap.find(successor);
            if (it != indexMap.end())
            {
                predecessors[it->second].push_back(block.name);
            }
        }
    }
    return predecessors;
}

void emitPhiAssignments(const ir::BasicBlock& block, const std::vector<std::string>& predecessors,
                        const std::unordered_map<std::string, std::string>& blockNames,
                        LoweringContext& context, CppEmitter& emitter)
{
    for (const auto& instruction : block.instructions)
    {
        if (instruction.opcode != ir::Opcode::PHI)
        {
            continue;
        }
        if (instruction.outputs.empty() || instruction.inputs.size() != predecessors.size())
        {
            emitter.writeLine("// TODO: malformed phi node");
            continue;
        }
        std::string dest = valueToExpr(instruction.outputs.front(), context);
        for (size_t index = 0; index < predecessors.size(); ++index)
        {
            std::string condition =
                "previousBlock == " + resolveBlockId(predecessors[index], blockNames);
            if (index == 0)
            {
                emitter.openBlock("if (" + condition + ")");
            }
            else
            {
                emitter.openBlock("else if (" + condition + ")");
            }
            emitter.writeLine(dest + " = " + valueToExpr(instruction.inputs[index], context) + ";");
            emitter.closeBlock();
        }
        if (!predecessors.empty())
        {
            emitter.openBlock("else");
            emitter.writeLine(dest + " = " + valueToExpr(instruction.inputs.front(), context) +
                              ";");
            emitter.closeBlock();
        }
    }
}

} // namespace recompiler
} // namespace psxrecomp
