#include "codegen_lowering_detail.h"
#include "codegen_lowering_gte.h"
#include "codegen_lowering_helpers.h"

#include <iomanip>
#include <optional>
#include <sstream>

namespace psxrecomp
{
namespace recompiler
{

void emitInstruction(const ir::Instruction& instruction, const ir::BasicBlock& block,
                     const std::unordered_map<std::string, std::string>& blockNames,
                     LoweringContext& context, CppEmitter& emitter)
{
    if (context.generateComments)
    {
        std::string comment;
        if (instruction.sourceAsm.has_value())
        {
            const std::optional<Address> asmAddress = instruction.sourceAsmAddress.has_value()
                                                          ? instruction.sourceAsmAddress
                                                          : instruction.sourceAddress;
            if (asmAddress.has_value())
            {
                std::ostringstream stream;
                stream << "// 0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
                       << *asmAddress << ": " << *instruction.sourceAsm;
                comment = stream.str();
            }
            else
            {
                comment = "// " + *instruction.sourceAsm;
            }
        }
        else
        {
            comment = "// " + opcodeToComment(instruction.opcode);
            if (instruction.sourceAddress.has_value())
            {
                std::ostringstream stream;
                stream << comment << " @0x" << std::hex << *instruction.sourceAddress;
                comment = stream.str();
            }
        }
        emitter.writeLine(comment);
    }

    enum class ZeroOptimization
    {
        None,
        Elide,
        ZeroResult
    };

    auto writeBinaryOp = [&](const char* op, ZeroOptimization optimization)
    {
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            throwLoweringError(instruction, "Malformed binary operation");
        }
        const std::optional<std::string> lhs =
            valueToWriteExpr(instruction.outputs.front(), context);
        if (!lhs.has_value())
        {
            return;
        }
        std::string rhsA = valueToExpr(instruction.inputs[0], context);
        std::string rhsB = valueToExpr(instruction.inputs[1], context);

        if (context.enableOptimizations && instruction.inputs[1].kind == ir::ValueKind::IMMEDIATE &&
            instruction.inputs[1].immediate == 0)
        {
            if (optimization == ZeroOptimization::Elide)
            {
                emitter.writeLine(*lhs + " = " + rhsA + ";");
                return;
            }
            if (optimization == ZeroOptimization::ZeroResult)
            {
                emitter.writeLine(*lhs + " = 0;");
                return;
            }
            return;
        }
        emitter.writeLine(*lhs + " = " + rhsA + " " + op + " " + rhsB + ";");
    };

    auto writeCompareOp = [&](const char* op)
    {
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            throwLoweringError(instruction, "Malformed compare operation");
        }
        const std::optional<std::string> lhs =
            valueToWriteExpr(instruction.outputs.front(), context);
        if (!lhs.has_value())
        {
            return;
        }
        std::string rhsA = valueToExpr(instruction.inputs[0], context);
        std::string rhsB = valueToExpr(instruction.inputs[1], context);
        emitter.writeLine(*lhs + " = (static_cast<s32>(" + rhsA + ") " + op + " static_cast<s32>(" +
                          rhsB + "));");
    };

    if (emitControlFlowInstruction(instruction, block, blockNames, context, emitter))
    {
        return;
    }

    if (emitGteInstruction(instruction, block, blockNames, context, emitter))
    {
        return;
    }

    if (emitMemoryAndSystemInstruction(instruction, block, blockNames, context, emitter))
    {
        return;
    }

    switch (instruction.opcode)
    {
    case ir::Opcode::NOP:
        emitter.writeLine(";");
        break;
    case ir::Opcode::PHI:
        break;
    case ir::Opcode::MOVE:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            const std::optional<std::string> dest =
                valueToWriteExpr(instruction.outputs.front(), context);
            std::string source = valueToExpr(instruction.inputs.front(), context);
            if (dest.has_value() && (!context.enableOptimizations || *dest != source))
            {
                emitter.writeLine(*dest + " = " + source + ";");
            }
        }
        break;
    case ir::Opcode::ADD_TRAP:
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            throwLoweringError(instruction, "Malformed trapping add operation");
        }
        emitter.openBlock("");
        emitter.writeLine("const u32 lhsValue = " + valueToExpr(instruction.inputs[0], context) +
                          ";");
        emitter.writeLine("const u32 rhsValue = " + valueToExpr(instruction.inputs[1], context) +
                          ";");
        emitter.writeLine("const u32 resultValue = lhsValue + rhsValue;");
        emitter.openBlock(
            "if (((~(lhsValue ^ rhsValue) & (lhsValue ^ resultValue)) & 0x80000000u) != 0)");
        emitter.writeLine(
            "raiseCpuException(context, "
            "static_cast<u32>(runtime::Cop0::ExceptionCode::ArithmeticOverflow), " +
            instructionSourcePcExpr(instruction) + ", " +
            (instructionIsInDelaySlot(instruction) ? std::string("true") : std::string("false")) +
            ");");
        emitter.closeBlock();
        if (const std::optional<std::string> dest =
                valueToWriteExpr(instruction.outputs.front(), context);
            dest.has_value())
        {
            emitter.writeLine(*dest + " = resultValue;");
        }
        emitter.closeBlock();
        break;
    case ir::Opcode::ADD:
        writeBinaryOp("+", ZeroOptimization::Elide);
        break;
    case ir::Opcode::SUB_TRAP:
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            throwLoweringError(instruction, "Malformed trapping subtract operation");
        }
        emitter.openBlock("");
        emitter.writeLine("const u32 lhsValue = " + valueToExpr(instruction.inputs[0], context) +
                          ";");
        emitter.writeLine("const u32 rhsValue = " + valueToExpr(instruction.inputs[1], context) +
                          ";");
        emitter.writeLine("const u32 resultValue = lhsValue - rhsValue;");
        emitter.openBlock(
            "if ((((lhsValue ^ rhsValue) & (lhsValue ^ resultValue)) & 0x80000000u) != 0)");
        emitter.writeLine(
            "raiseCpuException(context, "
            "static_cast<u32>(runtime::Cop0::ExceptionCode::ArithmeticOverflow), " +
            instructionSourcePcExpr(instruction) + ", " +
            (instructionIsInDelaySlot(instruction) ? std::string("true") : std::string("false")) +
            ");");
        emitter.closeBlock();
        if (const std::optional<std::string> dest =
                valueToWriteExpr(instruction.outputs.front(), context);
            dest.has_value())
        {
            emitter.writeLine(*dest + " = resultValue;");
        }
        emitter.closeBlock();
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
    case ir::Opcode::SHL:
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            throwLoweringError(instruction, "Malformed shift operation");
        }
        if (const std::optional<std::string> dest =
                valueToWriteExpr(instruction.outputs.front(), context);
            dest.has_value())
        {
            emitter.writeLine(*dest + " = " + valueToExpr(instruction.inputs[0], context) +
                              " << (" + valueToExpr(instruction.inputs[1], context) + " & 0x1F);");
        }
        break;
    case ir::Opcode::SHR_LOGICAL:
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            throwLoweringError(instruction, "Malformed shift operation");
        }
        if (const std::optional<std::string> dest =
                valueToWriteExpr(instruction.outputs.front(), context);
            dest.has_value())
        {
            emitter.writeLine(*dest + " = static_cast<u32>(" +
                              valueToExpr(instruction.inputs[0], context) + ") >> (" +
                              valueToExpr(instruction.inputs[1], context) + " & 0x1F);");
        }
        break;
    case ir::Opcode::SHR_ARITH:
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            throwLoweringError(instruction, "Malformed shift operation");
        }
        if (const std::optional<std::string> dest =
                valueToWriteExpr(instruction.outputs.front(), context);
            dest.has_value())
        {
            emitter.writeLine(*dest +
                              " = static_cast<u32>("
                              "static_cast<s32>(" +
                              valueToExpr(instruction.inputs[0], context) + ") >> (" +
                              valueToExpr(instruction.inputs[1], context) + " & 0x1F));");
        }
        break;
    case ir::Opcode::MUL:
    case ir::Opcode::MULU:
        if (instruction.outputs.size() >= 2 && instruction.inputs.size() >= 2)
        {
            const std::optional<std::string> hi = valueToWriteExpr(instruction.outputs[0], context);
            const std::optional<std::string> lo = valueToWriteExpr(instruction.outputs[1], context);
            std::string lhs = valueToExpr(instruction.inputs[0], context);
            std::string rhs = valueToExpr(instruction.inputs[1], context);
            if (instruction.opcode == ir::Opcode::MUL)
            {
                const std::string productExpr = "(static_cast<s64>(static_cast<s32>(" + lhs +
                                                ")) * "
                                                "static_cast<s64>(static_cast<s32>(" +
                                                rhs + ")))";
                if (lo.has_value())
                {
                    emitter.writeLine(*lo + " = static_cast<u32>(" + productExpr + ");");
                }
                if (hi.has_value())
                {
                    emitter.writeLine(*hi + " = static_cast<u32>(static_cast<u64>(" + productExpr +
                                      ") >> 32);");
                }
            }
            else
            {
                const std::string productExpr =
                    "(static_cast<u64>(" + lhs + ") * static_cast<u64>(" + rhs + "))";
                if (lo.has_value())
                {
                    emitter.writeLine(*lo + " = static_cast<u32>(" + productExpr + ");");
                }
                if (hi.has_value())
                {
                    emitter.writeLine(*hi + " = static_cast<u32>(" + productExpr + " >> 32);");
                }
            }
        }
        break;
    case ir::Opcode::DIV:
    case ir::Opcode::DIVU:
        if (instruction.outputs.size() >= 2 && instruction.inputs.size() >= 2)
        {
            const std::optional<std::string> hi = valueToWriteExpr(instruction.outputs[0], context);
            const std::optional<std::string> lo = valueToWriteExpr(instruction.outputs[1], context);
            std::string lhs = valueToExpr(instruction.inputs[0], context);
            std::string rhs = valueToExpr(instruction.inputs[1], context);
            emitter.openBlock("if (" + rhs + " == 0)");
            if (instruction.opcode == ir::Opcode::DIV)
            {
                emitter.writeLine("const s32 dividend = static_cast<s32>(" + lhs + ");");
                if (hi.has_value())
                {
                    emitter.writeLine(*hi + " = static_cast<u32>(dividend);");
                }
                if (lo.has_value())
                {
                    emitter.writeLine(*lo + " = (dividend >= 0) ? 0xFFFFFFFFu : 1u;");
                }
            }
            else
            {
                if (hi.has_value())
                {
                    emitter.writeLine(*hi + " = static_cast<u32>(" + lhs + ");");
                }
                if (lo.has_value())
                {
                    emitter.writeLine(*lo + " = 0xFFFFFFFFu;");
                }
            }
            emitter.closeBlock();
            emitter.openBlock("else");
            if (instruction.opcode == ir::Opcode::DIV)
            {
                emitter.openBlock("if (static_cast<u32>(" + lhs + ") == 0x80000000u && " + rhs +
                                  " == 0xFFFFFFFFu)");
                if (lo.has_value())
                {
                    emitter.writeLine(*lo + " = 0x80000000u;");
                }
                if (hi.has_value())
                {
                    emitter.writeLine(*hi + " = 0u;");
                }
                emitter.closeBlock();
                emitter.openBlock("else");
                if (lo.has_value())
                {
                    emitter.writeLine(*lo + " = static_cast<u32>(static_cast<s32>(" + lhs +
                                      ") / static_cast<s32>(" + rhs + "));");
                }
                if (hi.has_value())
                {
                    emitter.writeLine(*hi + " = static_cast<u32>(static_cast<s32>(" + lhs +
                                      ") % static_cast<s32>(" + rhs + "));");
                }
                emitter.closeBlock();
            }
            else
            {
                if (lo.has_value())
                {
                    emitter.writeLine(*lo + " = static_cast<u32>(" + lhs + ") / static_cast<u32>(" +
                                      rhs + ");");
                }
                if (hi.has_value())
                {
                    emitter.writeLine(*hi + " = static_cast<u32>(" + lhs + ") % static_cast<u32>(" +
                                      rhs + ");");
                }
            }
            emitter.closeBlock();
        }
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
    case ir::Opcode::COMPARE_LTU:
        if (!instruction.outputs.empty() && instruction.inputs.size() >= 2)
        {
            if (const std::optional<std::string> dest =
                    valueToWriteExpr(instruction.outputs.front(), context);
                dest.has_value())
            {
                const std::string lhs = valueToExpr(instruction.inputs[0], context);
                const std::string rhs = valueToExpr(instruction.inputs[1], context);
                emitter.writeLine(*dest + " = static_cast<u32>(static_cast<u32>(" + lhs +
                                  ") < static_cast<u32>(" + rhs + "));");
            }
        }
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
    case ir::Opcode::BRANCH:
    case ir::Opcode::JUMP:
    case ir::Opcode::CALL:
        // Control-flow ops are lowered in emitControlFlowInstruction above.
        break;
    default:
        throwLoweringError(instruction, "Unsupported opcode reached the instruction lowerer");
    }
}

} // namespace recompiler
} // namespace psxrecomp
