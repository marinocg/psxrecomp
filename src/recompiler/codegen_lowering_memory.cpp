#include "codegen_lowering_detail.h"
#include "codegen_lowering_helpers.h"

#include <optional>
#include <sstream>

namespace psxrecomp
{
namespace recompiler
{

bool emitMemoryAndSystemInstruction(const ir::Instruction& instruction, const ir::BasicBlock& block,
                                    const std::unordered_map<std::string, std::string>& blockNames,
                                    LoweringContext& context, CppEmitter& emitter)
{
    (void)block;
    (void)blockNames;

    switch (instruction.opcode)
    {
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
            emitter.writeLine(dest + " = readMemory32(context, " + address + ");");
            emitter.writeLine("traceInterestingLoad(context, " + address + ", " + dest + ", " +
                              sourcePc + ");");
        }
        return true;
    case ir::Opcode::LOAD8:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMemory8s(context, " + address + ");");
        }
        return true;
    case ir::Opcode::LOAD8U:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMemory8(context, " + address + ");");
        }
        return true;
    case ir::Opcode::LOAD16:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMemory16s(context, " + address + ");");
        }
        return true;
    case ir::Opcode::LOAD16U:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMemory16(context, " + address + ");");
        }
        return true;
    case ir::Opcode::LOAD_LEFT:
        if (!instruction.outputs.empty() && instruction.inputs.size() >= 2)
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            std::string sourcePc = "0";
            if (instruction.sourceAddress.has_value())
            {
                std::ostringstream sourceStream;
                sourceStream << "0x" << std::hex << instruction.sourceAddress.value();
                sourcePc = sourceStream.str();
            }
            emitter.writeLine(dest + " = readMemoryLwl(context, " + address + ", " + value + ");");
            emitter.writeLine("traceInterestingLoad(context, " + address + ", " + dest + ", " +
                              sourcePc + ");");
        }
        return true;
    case ir::Opcode::LOAD_RIGHT:
        if (!instruction.outputs.empty() && instruction.inputs.size() >= 2)
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            std::string sourcePc = "0";
            if (instruction.sourceAddress.has_value())
            {
                std::ostringstream sourceStream;
                sourceStream << "0x" << std::hex << instruction.sourceAddress.value();
                sourcePc = sourceStream.str();
            }
            emitter.writeLine(dest + " = readMemoryLwr(context, " + address + ", " + value + ");");
            emitter.writeLine("traceInterestingLoad(context, " + address + ", " + dest + ", " +
                              sourcePc + ");");
        }
        return true;
    case ir::Opcode::STORE:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMemory32(context, " + address + ", " + value + ");");
        }
        return true;
    case ir::Opcode::STORE8:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMemory8(context, " + address + ", " + value + ");");
        }
        return true;
    case ir::Opcode::STORE16:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMemory16(context, " + address + ", " + value + ");");
        }
        return true;
    case ir::Opcode::STORE_LEFT:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMemorySwl(context, " + address + ", " + value + ");");
        }
        return true;
    case ir::Opcode::STORE_RIGHT:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMemorySwr(context, " + address + ", " + value + ");");
        }
        return true;
    case ir::Opcode::MMIO_LOAD8:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMmio8s(context, " + address + ");");
        }
        else
        {
            throwLoweringError(instruction, "Malformed MMIO byte load");
        }
        return true;
    case ir::Opcode::MMIO_LOAD8U:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMmio8(context, " + address + ");");
        }
        else
        {
            throwLoweringError(instruction, "Malformed MMIO byte load");
        }
        return true;
    case ir::Opcode::MMIO_LOAD16:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMmio16s(context, " + address + ");");
        }
        else
        {
            throwLoweringError(instruction, "Malformed MMIO halfword load");
        }
        return true;
    case ir::Opcode::MMIO_LOAD16U:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMmio16(context, " + address + ");");
        }
        else
        {
            throwLoweringError(instruction, "Malformed MMIO halfword load");
        }
        return true;
    case ir::Opcode::MMIO_LOAD:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMmio32(context, " + address + ");");
        }
        else
        {
            throwLoweringError(instruction, "Malformed MMIO word load");
        }
        return true;
    case ir::Opcode::MMIO_STORE8:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMmio8(context, " + address + ", " + value + ");");
        }
        else
        {
            throwLoweringError(instruction, "Malformed MMIO byte store");
        }
        return true;
    case ir::Opcode::MMIO_STORE16:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMmio16(context, " + address + ", " + value + ");");
        }
        else
        {
            throwLoweringError(instruction, "Malformed MMIO halfword store");
        }
        return true;
    case ir::Opcode::MMIO_STORE:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMmio32(context, " + address + ", " + value + ");");
        }
        else
        {
            throwLoweringError(instruction, "Malformed MMIO word store");
        }
        return true;
    case ir::Opcode::COP0_MFC:
        if (!instruction.outputs.empty() && !instruction.inputs.empty() &&
            instruction.inputs[0].kind == ir::ValueKind::IMMEDIATE)
        {
            const std::string dest = valueToExpr(instruction.outputs.front(), context);
            const std::string rd = valueToExpr(instruction.inputs[0], context);
            emitter.writeLine(dest + " = context.system.cop0().mfc0(static_cast<u8>(" + rd + "));");
        }
        return true;
    case ir::Opcode::COP0_MTC:
        if (instruction.inputs.size() >= 2 &&
            instruction.inputs[0].kind == ir::ValueKind::IMMEDIATE)
        {
            const std::string rd = valueToExpr(instruction.inputs[0], context);
            const std::string source = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("context.system.cop0().mtc0(static_cast<u8>(" + rd + "), " + source +
                              ");");
        }
        return true;
    case ir::Opcode::COP0_RFE:
        emitter.writeLine("context.system.cop0().rfe();");
        return true;
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
        return true;
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
        return true;
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
        return true;
    }
    case ir::Opcode::RETURN:
        emitter.writeLine("return true;");
        return true;
    default:
        return false;
    }
}

} // namespace recompiler
} // namespace psxrecomp