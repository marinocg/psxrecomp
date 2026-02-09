#include "psxrecomp/recompiler/codegen.h"

#include "codegen_helpers.h"
#include "cpp_emitter.h"

#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace
{
struct LoweringContext
{
    std::map<u32, std::string> temporaries;
    bool generateComments = true;
    bool enableOptimizations = true;
};

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
    case ir::Opcode::COMPARE_EQ:
        return "cmp_eq";
    case ir::Opcode::COMPARE_NE:
        return "cmp_ne";
    case ir::Opcode::COMPARE_LT:
        return "cmp_lt";
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
    case ir::Opcode::BRANCH:
        return "branch";
    case ir::Opcode::JUMP:
        return "jump";
    case ir::Opcode::CALL:
        return "call";
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

enum class ZeroOptimization
{
    None,
    Elide,
    ZeroResult
};

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

void emitInstruction(const ir::Instruction& instruction, const ir::BasicBlock& block,
                     const std::unordered_map<std::string, std::string>& blockNames,
                     LoweringContext& context, CppEmitter& emitter)
{
    if (context.generateComments)
    {
        std::string comment = "// " + opcodeToComment(instruction.opcode);
        if (instruction.sourceAddress.has_value())
        {
            std::ostringstream stream;
            stream << comment << " @0x" << std::hex << *instruction.sourceAddress;
            comment = stream.str();
        }
        emitter.writeLine(comment);
    }

    auto writeBinaryOp = [&](const char* op, ZeroOptimization optimization)
    {
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            emitter.writeLine("// TODO: malformed binary op");
            return;
        }
        std::string lhs = valueToExpr(instruction.outputs.front(), context);
        std::string rhsA = valueToExpr(instruction.inputs[0], context);
        std::string rhsB = valueToExpr(instruction.inputs[1], context);

        if (context.enableOptimizations && instruction.inputs[1].kind == ir::ValueKind::IMMEDIATE &&
            instruction.inputs[1].immediate == 0)
        {
            if (optimization == ZeroOptimization::Elide)
            {
                emitter.writeLine(lhs + " = " + rhsA + ";");
                return;
            }
            if (optimization == ZeroOptimization::ZeroResult)
            {
                emitter.writeLine(lhs + " = 0;");
                return;
            }
            return;
        }
        emitter.writeLine(lhs + " = " + rhsA + " " + op + " " + rhsB + ";");
    };

    auto writeCompareOp = [&](const char* op)
    {
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            emitter.writeLine("// TODO: malformed compare op");
            return;
        }
        std::string lhs = valueToExpr(instruction.outputs.front(), context);
        std::string rhsA = valueToExpr(instruction.inputs[0], context);
        std::string rhsB = valueToExpr(instruction.inputs[1], context);
        emitter.writeLine(lhs + " = (static_cast<s32>(" + rhsA + ") " + op + " static_cast<s32>(" +
                          rhsB + "));");
    };

    switch (instruction.opcode)
    {
    case ir::Opcode::NOP:
        emitter.writeLine(";");
        break;
    case ir::Opcode::PHI:
        emitter.writeLine("// TODO: phi node lowering");
        emitter.writeLine("std::abort();");
        break;
    case ir::Opcode::MOVE:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string source = valueToExpr(instruction.inputs.front(), context);
            if (!context.enableOptimizations || dest != source)
            {
                emitter.writeLine(dest + " = " + source + ";");
            }
        }
        break;
    case ir::Opcode::ADD:
        writeBinaryOp("+", ZeroOptimization::Elide);
        break;
    case ir::Opcode::SUB:
        writeBinaryOp("-", ZeroOptimization::Elide);
        break;
    case ir::Opcode::AND:
        writeBinaryOp("&", ZeroOptimization::ZeroResult);
        break;
    case ir::Opcode::OR:
        writeBinaryOp("|", ZeroOptimization::Elide);
        break;
    case ir::Opcode::XOR:
        writeBinaryOp("^", ZeroOptimization::Elide);
        break;
    case ir::Opcode::COMPARE_EQ:
        writeCompareOp("==");
        break;
    case ir::Opcode::COMPARE_NE:
        writeCompareOp("!=");
        break;
    case ir::Opcode::COMPARE_LT:
        writeCompareOp("<");
        break;
    case ir::Opcode::COMPARE_LE:
        writeCompareOp("<=");
        break;
    case ir::Opcode::COMPARE_GT:
        writeCompareOp(">");
        break;
    case ir::Opcode::COMPARE_GE:
        writeCompareOp(">=");
        break;
    case ir::Opcode::LOAD:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMemory32(context.system, " + address + ");");
        }
        break;
    case ir::Opcode::STORE:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMemory32(context.system, " + address + ", " + value + ");");
        }
        break;
    case ir::Opcode::BRANCH:
        if (!instruction.inputs.empty())
        {
            std::string cond = valueToExpr(instruction.inputs.front(), context);
            if (block.successors.size() >= 2)
            {
                emitter.openBlock("if (" + cond + ")");
                emitter.writeLine("block = " + resolveBlockId(block.successors[0], blockNames) +
                                  ";");
                emitter.writeLine("continue;");
                emitter.closeBlock();
                emitter.openBlock("else");
                emitter.writeLine("block = " + resolveBlockId(block.successors[1], blockNames) +
                                  ";");
                emitter.writeLine("continue;");
                emitter.closeBlock();
            }
            else if (block.successors.size() == 1)
            {
                emitter.openBlock("if (" + cond + ")");
                emitter.writeLine("block = " + resolveBlockId(block.successors[0], blockNames) +
                                  ";");
                emitter.writeLine("continue;");
                emitter.closeBlock();
                emitter.openBlock("else");
                emitter.writeLine("return;");
                emitter.closeBlock();
            }
        }
        break;
    case ir::Opcode::JUMP:
        if (!block.successors.empty())
        {
            emitter.writeLine("block = " + resolveBlockId(block.successors.front(), blockNames) +
                              ";");
            emitter.writeLine("continue;");
        }
        break;
    case ir::Opcode::CALL:
        if (!instruction.inputs.empty())
        {
            std::string target = valueToExpr(instruction.inputs.front(), context);
            emitter.openBlock("if (!callIntrinsic(context.system, " + target + "))");
            emitter.writeLine("std::abort();");
            emitter.closeBlock();
        }
        else
        {
            emitter.writeLine("// TODO: call lowering");
        }
        break;
    case ir::Opcode::RETURN:
        emitter.writeLine("return;");
        break;
    }
}
} // namespace

std::string CodeGenerator::generateFunctionDefinitions(const ir::Program& program) const
{
    CppEmitter emitter;
    std::unordered_set<std::string> usedFunctionNames;
    for (const auto& function : program.functions)
    {
        LoweringContext context;
        context.generateComments = m_options.generateComments;
        context.enableOptimizations = m_options.enableOptimizations;

        std::string functionName = uniquifyIdentifier(function.name, usedFunctionNames);
        emitter.writeLine("void " + functionName + "(RecompilerContext& context)");
        emitter.openBlock("");
        if (function.blocks.empty())
        {
            emitter.writeLine("// TODO: empty function body");
            emitter.writeLine("return;");
            emitter.closeBlock();
            emitter.writeBlank();
            continue;
        }

        auto temporaries = collectTemporaries(function);
        for (u32 temporaryId : temporaries)
        {
            context.temporaries[temporaryId] = "temp" + std::to_string(temporaryId);
            emitter.writeLine("u32 " + context.temporaries[temporaryId] + " = 0;");
        }

        emitter.writeBlank();
        std::unordered_set<std::string> usedBlocks;
        std::unordered_map<std::string, std::string> blockNames;
        std::vector<std::string> orderedBlockNames;
        orderedBlockNames.reserve(function.blocks.size());
        for (const auto& block : function.blocks)
        {
            std::string uniqueName = uniquifyIdentifier(block.name, usedBlocks);
            blockNames.emplace(block.name, uniqueName);
            orderedBlockNames.push_back(uniqueName);
        }

        emitter.writeLine("enum class BlockId {");
        for (size_t index = 0; index < orderedBlockNames.size(); ++index)
        {
            emitter.writeLine("    " + orderedBlockNames[index] +
                              (index + 1 < orderedBlockNames.size() ? "," : ""));
        }
        emitter.writeLine("};");
        if (!orderedBlockNames.empty())
        {
            emitter.writeLine("BlockId block = BlockId::" + orderedBlockNames.front() + ";");
        }
        emitter.writeLine("while (true)");
        emitter.openBlock("");
        emitter.writeLine("switch (block)");
        emitter.openBlock("");
        for (const auto& block : function.blocks)
        {
            emitter.writeLine("case " + resolveBlockId(block.name, blockNames) + ":");
            emitter.openBlock("");
            for (const auto& instruction : block.instructions)
            {
                emitInstruction(instruction, block, blockNames, context, emitter);
            }
            if (block.instructions.empty() ||
                block.instructions.back().opcode != ir::Opcode::RETURN)
            {
                if (!block.successors.empty())
                {
                    emitter.writeLine(
                        "block = " + resolveBlockId(block.successors.front(), blockNames) + ";");
                    emitter.writeLine("continue;");
                }
                else
                {
                    emitter.writeLine("return;");
                }
            }
            emitter.closeBlock();
        }
        emitter.writeLine("default:");
        emitter.openBlock("");
        emitter.writeLine("return;");
        emitter.closeBlock();
        emitter.closeBlock();
        emitter.closeBlock();
        emitter.closeBlock();
        emitter.writeBlank();
    }
    return emitter.str();
}

} // namespace recompiler
} // namespace psxrecomp
