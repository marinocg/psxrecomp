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

    if (emitControlFlowInstruction(instruction, block, blockNames, context, emitter))
    {
        return;
    }

    if (emitGteInstruction(instruction, block, blockNames, context, emitter))
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
    case ir::Opcode::SHL:
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            emitter.writeLine("// TODO: malformed shift op");
            break;
        }
        emitter.writeLine(valueToExpr(instruction.outputs.front(), context) + " = " +
                          valueToExpr(instruction.inputs[0], context) + " << (" +
                          valueToExpr(instruction.inputs[1], context) + " & 0x1F);");
        break;
    case ir::Opcode::SHR_LOGICAL:
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            emitter.writeLine("// TODO: malformed shift op");
            break;
        }
        emitter.writeLine(valueToExpr(instruction.outputs.front(), context) + " = " +
                          valueToExpr(instruction.inputs[0], context) + " >> (" +
                          valueToExpr(instruction.inputs[1], context) + " & 0x1F);");
        break;
    case ir::Opcode::SHR_ARITH:
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            emitter.writeLine("// TODO: malformed shift op");
            break;
        }
        emitter.writeLine(valueToExpr(instruction.outputs.front(), context) +
                          " = static_cast<u32>("
                          "static_cast<s32>(" +
                          valueToExpr(instruction.inputs[0], context) + ") >> (" +
                          valueToExpr(instruction.inputs[1], context) + " & 0x1F));");
        break;
    case ir::Opcode::MUL:
    case ir::Opcode::MULU:
        if (instruction.outputs.size() >= 2 && instruction.inputs.size() >= 2)
        {
            std::string hi = valueToExpr(instruction.outputs[0], context);
            std::string lo = valueToExpr(instruction.outputs[1], context);
            std::string lhs = valueToExpr(instruction.inputs[0], context);
            std::string rhs = valueToExpr(instruction.inputs[1], context);
            if (instruction.opcode == ir::Opcode::MUL)
            {
                const std::string productExpr = "(static_cast<s64>(static_cast<s32>(" + lhs +
                                                ")) * "
                                                "static_cast<s64>(static_cast<s32>(" +
                                                rhs + ")))";
                emitter.writeLine(lo + " = static_cast<u32>(" + productExpr + ");");
                emitter.writeLine(hi + " = static_cast<u32>(static_cast<u64>(" + productExpr +
                                  ") >> 32);");
            }
            else
            {
                const std::string productExpr =
                    "(static_cast<u64>(" + lhs + ") * static_cast<u64>(" + rhs + "))";
                emitter.writeLine(lo + " = static_cast<u32>(" + productExpr + ");");
                emitter.writeLine(hi + " = static_cast<u32>(" + productExpr + " >> 32);");
            }
        }
        break;
    case ir::Opcode::DIV:
    case ir::Opcode::DIVU:
        if (instruction.outputs.size() >= 2 && instruction.inputs.size() >= 2)
        {
            std::string hi = valueToExpr(instruction.outputs[0], context);
            std::string lo = valueToExpr(instruction.outputs[1], context);
            std::string lhs = valueToExpr(instruction.inputs[0], context);
            std::string rhs = valueToExpr(instruction.inputs[1], context);
            emitter.openBlock("if (" + rhs + " == 0)");
            emitter.writeLine(hi + " = 0;");
            emitter.writeLine(lo + " = 0;");
            emitter.closeBlock();
            emitter.openBlock("else");
            if (instruction.opcode == ir::Opcode::DIV)
            {
                emitter.writeLine(lo + " = static_cast<u32>(static_cast<s32>(" + lhs +
                                  ") / static_cast<s32>(" + rhs + "));");
                emitter.writeLine(hi + " = static_cast<u32>(static_cast<s32>(" + lhs +
                                  ") % static_cast<s32>(" + rhs + "));");
            }
            else
            {
                emitter.writeLine(lo + " = static_cast<u32>(" + lhs + ") / static_cast<u32>(" +
                                  rhs + ");");
                emitter.writeLine(hi + " = static_cast<u32>(" + lhs + ") % static_cast<u32>(" +
                                  rhs + ");");
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
            const std::string dest = valueToExpr(instruction.outputs.front(), context);
            const std::string lhs = valueToExpr(instruction.inputs[0], context);
            const std::string rhs = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine(dest + " = static_cast<u32>(static_cast<u32>(" + lhs +
                              ") < "
                              "static_cast<u32>(" +
                              rhs + "));");
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
    case ir::Opcode::LOAD:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            std::string sourcePc = "0";
            if (instruction.sourceAddress.has_value())
            {
                std::ostringstream sourceStream;
                sourceStream << "0x" << std::hex << instruction.sourceAddress.value();
                sourcePc = sourceStream.str();
            }
            emitter.writeLine(dest + " = readMemory32(context.system, " + address + ");");
            emitter.writeLine("traceInterestingLoad(context, " + address + ", " + dest + ", " +
                              sourcePc + ");");
        }
        break;
    case ir::Opcode::LOAD8:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMemory8s(context.system, " + address + ");");
        }
        break;
    case ir::Opcode::LOAD8U:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMemory8(context.system, " + address + ");");
        }
        break;
    case ir::Opcode::LOAD16:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMemory16s(context.system, " + address + ");");
        }
        break;
    case ir::Opcode::LOAD16U:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMemory16(context.system, " + address + ");");
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
    case ir::Opcode::STORE8:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMemory8(context.system, " + address + ", " + value + ");");
        }
        break;
    case ir::Opcode::STORE16:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMemory16(context.system, " + address + ", " + value + ");");
        }
        break;
    case ir::Opcode::MMIO_LOAD:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMmio32(context.system, " + address + ");");
        }
        break;
    case ir::Opcode::MMIO_STORE:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMmio32(context.system, " + address + ", " + value + ");");
        }
        break;
    case ir::Opcode::COP0_MFC:
        if (!instruction.outputs.empty() && !instruction.inputs.empty() &&
            instruction.inputs[0].kind == ir::ValueKind::IMMEDIATE)
        {
            const std::string dest = valueToExpr(instruction.outputs.front(), context);
            const std::string rd = valueToExpr(instruction.inputs[0], context);
            emitter.writeLine(dest + " = context.system.cop0().mfc0(static_cast<u8>(" + rd + "));");
        }
        break;
    case ir::Opcode::COP0_MTC:
        if (instruction.inputs.size() >= 2 &&
            instruction.inputs[0].kind == ir::ValueKind::IMMEDIATE)
        {
            const std::string rd = valueToExpr(instruction.inputs[0], context);
            const std::string source = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("context.system.cop0().mtc0(static_cast<u8>(" + rd + "), " + source +
                              ");");
        }
        break;
    case ir::Opcode::COP0_RFE:
        emitter.writeLine("context.system.cop0().rfe();");
        break;
    case ir::Opcode::CPU_EXCEPTION:
    {
        std::string code =
            instruction.inputs.empty() ? "0" : valueToExpr(instruction.inputs.front(), context);
        std::string inDelaySlot = "false";
        if (instruction.inputs.size() >= 2)
        {
            inDelaySlot = "(" + valueToExpr(instruction.inputs[1], context) + " != 0)";
        }
        std::string sourcePc = "0";
        if (instruction.sourceAddress.has_value())
        {
            std::ostringstream sourceStream;
            sourceStream << "0x" << std::hex << instruction.sourceAddress.value();
            sourcePc = sourceStream.str();
        }
        emitter.writeLine("raiseCpuException(context, " + code + ", " + sourcePc + ", " +
                          inDelaySlot + ");");
        break;
    }
    case ir::Opcode::SYSCALL:
        if (!instruction.inputs.empty())
        {
            std::string code = valueToExpr(instruction.inputs.front(), context);
            std::string sourcePc = "0";
            if (instruction.sourceAddress.has_value())
            {
                std::ostringstream sourceStream;
                sourceStream << "0x" << std::hex << instruction.sourceAddress.value();
                sourcePc = sourceStream.str();
            }
            emitter.writeLine("callSyscall(context, " + code + ", " + sourcePc + ");");
        }
        break;
    case ir::Opcode::TRAP:
    {
        std::string code =
            instruction.inputs.empty() ? "0" : valueToExpr(instruction.inputs.front(), context);
        std::string sourcePc = "0";
        if (instruction.sourceAddress.has_value())
        {
            std::ostringstream sourceStream;
            sourceStream << "0x" << std::hex << instruction.sourceAddress.value();
            sourcePc = sourceStream.str();
        }
        emitter.writeLine("triggerTrap(" + code + ", " + sourcePc + ");");
        break;
    }
    case ir::Opcode::RETURN:
        emitter.writeLine("return true;");
        break;
    case ir::Opcode::BRANCH:
    case ir::Opcode::JUMP:
    case ir::Opcode::CALL:
        // Control-flow ops are lowered in emitControlFlowInstruction above.
        break;
    default:
        // GTE ops are handled by emitGteInstruction above; all other unknown
        // opcodes are silently ignored.
        break;
    }
}

} // namespace recompiler
} // namespace psxrecomp
