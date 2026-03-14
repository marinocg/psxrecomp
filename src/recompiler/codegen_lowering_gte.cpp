#include "codegen_lowering_gte.h"
#include "codegen_lowering_detail.h"

namespace psxrecomp
{
namespace recompiler
{

namespace
{
void emitGteGuard(const ir::Instruction& instruction, CppEmitter& emitter)
{
    emitter.openBlock("if (!context.system.cop0().cop2Enabled())");
    emitter.writeLine("raiseCpuException(context, "
                      "static_cast<u32>(runtime::Cop0::ExceptionCode::CoprocessorUnusable), " +
                      instructionSourcePcExpr(instruction) + ", " +
                      std::string(instructionIsInDelaySlot(instruction) ? "true" : "false") + ");");
    emitter.closeBlock();
}
} // namespace

bool emitGteInstruction(
    const ir::Instruction& instruction, [[maybe_unused]] const ir::BasicBlock& block,
    [[maybe_unused]] const std::unordered_map<std::string, std::string>& blockNames,
    LoweringContext& context, CppEmitter& emitter)
{
    auto writeGteGuardPrefix = [&]() { emitGteGuard(instruction, emitter); };

    switch (instruction.opcode)
    {
    case ir::Opcode::GTE_MFC2:
        if (!instruction.outputs.empty() && !instruction.inputs.empty() &&
            instruction.inputs[0].kind == ir::ValueKind::IMMEDIATE)
        {
            const std::string dest = valueToExpr(instruction.outputs.front(), context);
            const std::string rd = valueToExpr(instruction.inputs[0], context);
            writeGteGuardPrefix();
            emitter.writeLine(dest + " = context.system.gte().mfc2(static_cast<u8>(" + rd + "));");
        }
        break;
    case ir::Opcode::GTE_MTC2:
        if (instruction.inputs.size() >= 2 &&
            instruction.inputs[0].kind == ir::ValueKind::IMMEDIATE)
        {
            const std::string rd = valueToExpr(instruction.inputs[0], context);
            const std::string source = valueToExpr(instruction.inputs[1], context);
            writeGteGuardPrefix();
            emitter.writeLine("context.system.gte().mtc2(static_cast<u8>(" + rd + "), " + source +
                              ");");
        }
        break;
    case ir::Opcode::GTE_CFC2:
        if (!instruction.outputs.empty() && !instruction.inputs.empty() &&
            instruction.inputs[0].kind == ir::ValueKind::IMMEDIATE)
        {
            const std::string dest = valueToExpr(instruction.outputs.front(), context);
            const std::string rd = valueToExpr(instruction.inputs[0], context);
            writeGteGuardPrefix();
            emitter.writeLine(dest + " = context.system.gte().cfc2(static_cast<u8>(" + rd + "));");
        }
        break;
    case ir::Opcode::GTE_CTC2:
        if (instruction.inputs.size() >= 2 &&
            instruction.inputs[0].kind == ir::ValueKind::IMMEDIATE)
        {
            const std::string rd = valueToExpr(instruction.inputs[0], context);
            const std::string source = valueToExpr(instruction.inputs[1], context);
            writeGteGuardPrefix();
            emitter.writeLine("context.system.gte().ctc2(static_cast<u8>(" + rd + "), " + source +
                              ");");
        }
        break;
    case ir::Opcode::GTE_LWC2:
        if (instruction.inputs.size() >= 2 &&
            instruction.inputs[0].kind == ir::ValueKind::IMMEDIATE)
        {
            const std::string rd = valueToExpr(instruction.inputs[0], context);
            const std::string address = valueToExpr(instruction.inputs[1], context);
            writeGteGuardPrefix();
            emitter.writeLine("context.system.gte().mtc2(static_cast<u8>(" + rd +
                              "), readMemory32(context, " + address + "));");
        }
        break;
    case ir::Opcode::GTE_SWC2:
        if (instruction.inputs.size() >= 2 &&
            instruction.inputs[0].kind == ir::ValueKind::IMMEDIATE)
        {
            const std::string rd = valueToExpr(instruction.inputs[0], context);
            const std::string address = valueToExpr(instruction.inputs[1], context);
            writeGteGuardPrefix();
            emitter.writeLine("writeMemory32(context, " + address +
                              ", context.system.gte().mfc2(static_cast<u8>(" + rd + ")));");
        }
        break;
    case ir::Opcode::GTE_EXEC:
        if (!instruction.inputs.empty())
        {
            const std::string rawEncoding = valueToExpr(instruction.inputs[0], context);
            writeGteGuardPrefix();
            emitter.writeLine("context.system.gte().exec(static_cast<u32>(" + rawEncoding + "));");
        }
        break;
    default:
        return false;
    }
    return true;
}

} // namespace recompiler
} // namespace psxrecomp
