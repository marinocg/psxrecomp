#include "codegen_lowering_detail.h"
#include "codegen_lowering_helpers.h"

#include <optional>
#include <sstream>

namespace psxrecomp
{
namespace recompiler
{

namespace
{

std::string controlFlowSourcePcExpr(const ir::Instruction& instruction)
{
    std::string sourcePc = "0";
    if (instruction.sourceAddress.has_value())
    {
        std::ostringstream sourceStream;
        sourceStream << "0x" << std::hex << instruction.sourceAddress.value();
        sourcePc = sourceStream.str();
    }
    return sourcePc;
}

std::optional<std::string> controlFlowTakenTargetLiteral(const ir::Instruction& instruction)
{
    if (instruction.inputs.size() >= 2 && instruction.inputs[1].kind == ir::ValueKind::ADDRESS)
    {
        std::ostringstream targetStream;
        targetStream << "0x" << std::hex << (instruction.inputs[1].address & 0x1FFFFFFFu);
        return targetStream.str();
    }
    return std::nullopt;
}

void emitBranchSuccessorTransfer(const std::string& successorName,
                                 const std::optional<std::string>& externalTarget,
                                 const std::string& sourcePc,
                                 const std::unordered_map<std::string, std::string>& blockNames,
                                 CppEmitter& emitter)
{
    const auto localIt = blockNames.find(successorName);
    if (localIt != blockNames.end())
    {
        emitter.writeLine("previousBlock = block;");
        emitter.writeLine("block = BlockId::" + localIt->second + ";");
        emitter.writeLine("continue;");
        return;
    }

    if (successorName == "block_external" && externalTarget.has_value())
    {
        emitter.openBlock("if (!jumpRecompiledFunction(context, " + *externalTarget + "))");
        emitter.writeLine("failUnsupportedJump(" + *externalTarget + ", " + sourcePc + ");");
        emitter.closeBlock();
        emitter.writeLine("return true;");
        return;
    }

    if (successorName.size() > 8 && successorName.substr(0, 8) == "block_0x")
    {
        const Address targetAddress =
            std::stoul(successorName.substr(6), nullptr, 16) & 0x1FFFFFFFu;
        std::ostringstream targetStream;
        targetStream << "0x" << std::hex << targetAddress;
        const std::string targetLiteral = targetStream.str();
        emitter.openBlock("if (!jumpRecompiledFunction(context, " + targetLiteral + "))");
        emitter.writeLine("failUnsupportedJump(" + targetLiteral + ", " + sourcePc + ");");
        emitter.closeBlock();
        emitter.writeLine("return true;");
        return;
    }

    emitter.writeLine("return false;");
}

void emitRegisterJumpTransfer(const DeferredControlTransfer& transfer,
                              const std::unordered_map<std::string, std::string>& blockNames,
                              CppEmitter& emitter)
{
    emitter.writeLine("const Address jumpTargetPhysical = " + transfer.targetExpr +
                      " & 0x1FFFFFFF;");
    emitter.openBlock("if (jumpTargetPhysical == 0)");
    emitter.writeLine("return true;");
    emitter.closeBlock();

    emitter.writeLine("switch (jumpTargetPhysical)");
    emitter.openBlock("");
    for (const auto& entry : blockNames)
    {
        const std::string& blockName = entry.first;
        if (blockName.size() <= 8 || blockName.substr(0, 8) != "block_0x")
        {
            continue;
        }

        const Address blockAddress = std::stoul(blockName.substr(6), nullptr, 16) & 0x1FFFFFFFu;
        std::ostringstream caseLine;
        caseLine << "case 0x" << std::hex << blockAddress << ":";
        emitter.writeLine(caseLine.str());
        emitter.openBlock("");
        emitter.writeLine("previousBlock = block;");
        emitter.writeLine("block = BlockId::" + entry.second + ";");
        emitter.writeLine("continue;");
        emitter.closeBlock();
    }
    emitter.writeLine("default:");
    emitter.openBlock("");
    emitter.writeLine("break;");
    emitter.closeBlock();
    emitter.closeBlock();

    emitter.openBlock("if (!callIntrinsic(context.system, " + transfer.targetExpr +
                      ", context.regs))");
    emitter.openBlock("if (!jumpRecompiledFunction(context, " + transfer.targetExpr + "))");
    emitter.writeLine("failUnsupportedJump(" + transfer.targetExpr + ", " + transfer.sourcePcExpr +
                      ");");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("return true;");
}

void emitAddressJumpTransfer(const DeferredControlTransfer& transfer,
                             const std::unordered_map<std::string, std::string>& blockNames,
                             CppEmitter& emitter)
{
    if (!transfer.successors.empty())
    {
        const std::string successor = resolveBlockId(transfer.successors.front(), blockNames);
        if (successor.find("block_external") != std::string::npos)
        {
            emitter.openBlock("if (!callIntrinsic(context.system, " + transfer.targetExpr +
                              ", context.regs))");
            emitter.openBlock("if (!jumpRecompiledFunction(context, " + transfer.targetExpr + "))");
            emitter.writeLine("failUnsupportedJump(" + transfer.targetExpr + ", " +
                              transfer.sourcePcExpr + ");");
            emitter.closeBlock();
            emitter.closeBlock();
            emitter.writeLine("return true;");
            return;
        }

        emitter.writeLine("previousBlock = block;");
        emitter.writeLine("block = " + successor + ";");
        emitter.writeLine("continue;");
        return;
    }

    emitter.writeLine("return false;");
}

void emitCallTransfer(const DeferredControlTransfer& transfer, CppEmitter& emitter)
{
    emitter.writeLine("traceInterestingCallsite(context, " + transfer.targetExpr + ", " +
                      transfer.sourcePcExpr + ", false);");
    emitter.openBlock("if (!callIntrinsic(context.system, " + transfer.targetExpr +
                      ", context.regs))");
    emitter.openBlock("if (!callRecompiledFunction(context, " + transfer.targetExpr + "))");
    emitter.writeLine("failUnsupportedCall(context, " + transfer.targetExpr + ", " +
                      transfer.sourcePcExpr + ");");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("traceInterestingCallsite(context, " + transfer.targetExpr + ", " +
                      transfer.sourcePcExpr + ", true);");
}

} // namespace

std::optional<DeferredControlTransfer>
buildDeferredControlTransfer(const ir::Instruction& instruction, const ir::BasicBlock& block,
                             const std::unordered_map<std::string, std::string>& blockNames,
                             LoweringContext& context)
{
    (void)blockNames;

    switch (instruction.opcode)
    {
    case ir::Opcode::BRANCH:
        if (instruction.inputs.empty())
        {
            throwLoweringError(instruction, "Branch instruction is missing a condition operand");
        }
        return DeferredControlTransfer{DeferredControlTransferKind::Branch,
                                       valueToExpr(instruction.inputs.front(), context),
                                       "",
                                       controlFlowSourcePcExpr(instruction),
                                       controlFlowTakenTargetLiteral(instruction),
                                       block.successors};
    case ir::Opcode::JUMP:
        if (instruction.inputs.empty())
        {
            throwLoweringError(instruction, "Jump instruction is missing a target operand");
        }
        if (instruction.inputs.front().kind == ir::ValueKind::REGISTER)
        {
            return DeferredControlTransfer{DeferredControlTransferKind::RegisterJump,
                                           "",
                                           valueToExpr(instruction.inputs.front(), context),
                                           controlFlowSourcePcExpr(instruction),
                                           std::nullopt,
                                           block.successors};
        }
        return DeferredControlTransfer{DeferredControlTransferKind::AddressJump,
                                       "",
                                       valueToExpr(instruction.inputs.front(), context),
                                       controlFlowSourcePcExpr(instruction),
                                       std::nullopt,
                                       block.successors};
    case ir::Opcode::CALL:
        if (instruction.inputs.empty())
        {
            throwLoweringError(instruction, "Call instruction is missing a target operand");
        }
        return DeferredControlTransfer{DeferredControlTransferKind::Call,
                                       "",
                                       valueToExpr(instruction.inputs.front(), context),
                                       controlFlowSourcePcExpr(instruction),
                                       std::nullopt,
                                       {}};
    case ir::Opcode::RETURN:
        return DeferredControlTransfer{DeferredControlTransferKind::Return,  "",           "",
                                       controlFlowSourcePcExpr(instruction), std::nullopt, {}};
    default:
        return std::nullopt;
    }
}

void emitDeferredControlTransfer(const DeferredControlTransfer& transfer,
                                 const ir::BasicBlock& block,
                                 const std::unordered_map<std::string, std::string>& blockNames,
                                 CppEmitter& emitter)
{
    (void)block;

    switch (transfer.kind)
    {
    case DeferredControlTransferKind::Branch:
        if (transfer.successors.size() >= 2)
        {
            emitter.openBlock("if (" + transfer.conditionExpr + ")");
            emitBranchSuccessorTransfer(transfer.successors[0], transfer.takenTargetLiteral,
                                        transfer.sourcePcExpr, blockNames, emitter);
            emitter.closeBlock();
            emitter.openBlock("else");
            emitBranchSuccessorTransfer(transfer.successors[1], std::nullopt, transfer.sourcePcExpr,
                                        blockNames, emitter);
            emitter.closeBlock();
            return;
        }
        if (transfer.successors.size() == 1)
        {
            emitter.openBlock("if (" + transfer.conditionExpr + ")");
            emitBranchSuccessorTransfer(transfer.successors[0], transfer.takenTargetLiteral,
                                        transfer.sourcePcExpr, blockNames, emitter);
            emitter.closeBlock();
            emitter.openBlock("else");
            emitter.writeLine("return true;");
            emitter.closeBlock();
            return;
        }
        emitter.writeLine("return false;");
        return;
    case DeferredControlTransferKind::RegisterJump:
        emitRegisterJumpTransfer(transfer, blockNames, emitter);
        return;
    case DeferredControlTransferKind::AddressJump:
        emitAddressJumpTransfer(transfer, blockNames, emitter);
        return;
    case DeferredControlTransferKind::Call:
        emitCallTransfer(transfer, emitter);
        return;
    case DeferredControlTransferKind::Return:
        emitter.writeLine("return true;");
        return;
    }
}

bool emitControlFlowInstruction(const ir::Instruction& instruction, const ir::BasicBlock& block,
                                const std::unordered_map<std::string, std::string>& blockNames,
                                LoweringContext& context, CppEmitter& emitter)
{
    if (context.deferControlTransfers)
    {
        if (std::optional<DeferredControlTransfer> transfer =
                buildDeferredControlTransfer(instruction, block, blockNames, context);
            transfer.has_value())
        {
            if (context.deferredTransfer.has_value())
            {
                throwLoweringError(instruction,
                                   "Multiple control transfers emitted for one source group");
            }
            context.deferredTransfer = std::move(transfer);
            return true;
        }
    }

    switch (instruction.opcode)
    {
    case ir::Opcode::BRANCH:
        if (!instruction.inputs.empty())
        {
            std::string cond = valueToExpr(instruction.inputs.front(), context);
            std::string sourcePc = controlFlowSourcePcExpr(instruction);
            std::optional<std::string> takenTargetLiteral =
                controlFlowTakenTargetLiteral(instruction);

            auto emitSuccessorTransfer = [&](const std::string& successorName,
                                             const std::optional<std::string>& externalTarget)
            {
                const auto localIt = blockNames.find(successorName);
                if (localIt != blockNames.end())
                {
                    emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
                    emitter.writeLine("previousBlock = block;");
                    emitter.writeLine("block = BlockId::" + localIt->second + ";");
                    emitter.writeLine("continue;");
                    return;
                }

                if (successorName == "block_external" && externalTarget.has_value())
                {
                    emitter.openBlock("if (!jumpRecompiledFunction(context, " + *externalTarget +
                                      "))");
                    emitter.writeLine("failUnsupportedJump(" + *externalTarget + ", " + sourcePc +
                                      ");");
                    emitter.closeBlock();
                    emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
                    emitter.writeLine("return true;");
                    return;
                }

                if (successorName.size() > 8 && successorName.substr(0, 8) == "block_0x")
                {
                    const Address targetAddress =
                        std::stoul(successorName.substr(6), nullptr, 16) & 0x1FFFFFFFu;
                    std::ostringstream targetStream;
                    targetStream << "0x" << std::hex << targetAddress;
                    const std::string targetLiteral = targetStream.str();
                    emitter.openBlock("if (!jumpRecompiledFunction(context, " + targetLiteral +
                                      "))");
                    emitter.writeLine("failUnsupportedJump(" + targetLiteral + ", " + sourcePc +
                                      ");");
                    emitter.closeBlock();
                    emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
                    emitter.writeLine("return true;");
                    return;
                }

                emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
                emitter.writeLine("return false;");
            };

            if (block.successors.size() >= 2)
            {
                emitter.openBlock("if (" + cond + ")");
                emitSuccessorTransfer(block.successors[0], takenTargetLiteral);
                emitter.closeBlock();
                emitter.openBlock("else");
                emitSuccessorTransfer(block.successors[1], std::nullopt);
                emitter.closeBlock();
            }
            else if (block.successors.size() == 1)
            {
                emitter.openBlock("if (" + cond + ")");
                emitSuccessorTransfer(block.successors[0], takenTargetLiteral);
                emitter.closeBlock();
                emitter.openBlock("else");
                emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
                emitter.writeLine("return true;");
                emitter.closeBlock();
            }
        }
        return true;
    case ir::Opcode::JUMP:
        if (!instruction.inputs.empty() &&
            instruction.inputs.front().kind == ir::ValueKind::REGISTER)
        {
            std::string target = valueToExpr(instruction.inputs.front(), context);
            std::string sourcePc = controlFlowSourcePcExpr(instruction);
            emitter.writeLine("const Address jumpTargetPhysical = " + target + " & 0x1FFFFFFF;");
            emitter.openBlock("if (jumpTargetPhysical == 0)");
            emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
            emitter.writeLine("return true;");
            emitter.closeBlock();

            emitter.writeLine("switch (jumpTargetPhysical)");
            emitter.openBlock("");
            for (const auto& entry : blockNames)
            {
                const std::string& blockName = entry.first;
                if (blockName.size() <= 8 || blockName.substr(0, 8) != "block_0x")
                {
                    continue;
                }

                const Address blockAddress =
                    std::stoul(blockName.substr(6), nullptr, 16) & 0x1FFFFFFFu;
                std::ostringstream caseLine;
                caseLine << "case 0x" << std::hex << blockAddress << ":";
                emitter.writeLine(caseLine.str());
                emitter.openBlock("");
                emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
                emitter.writeLine("previousBlock = block;");
                emitter.writeLine("block = BlockId::" + entry.second + ";");
                emitter.writeLine("continue;");
                emitter.closeBlock();
            }
            emitter.writeLine("default:");
            emitter.openBlock("");
            emitter.writeLine("break;");
            emitter.closeBlock();
            emitter.closeBlock();

            emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
            emitter.openBlock("if (!callIntrinsic(context.system, " + target + ", context.regs))");
            emitter.openBlock("if (!jumpRecompiledFunction(context, " + target + "))");
            emitter.writeLine("failUnsupportedJump(" + target + ", " + sourcePc + ");");
            emitter.closeBlock();
            emitter.closeBlock();
            emitter.writeLine("return true;");
        }
        else if (!block.successors.empty())
        {
            const std::string successor = resolveBlockId(block.successors.front(), blockNames);
            if (successor.find("block_external") != std::string::npos &&
                !instruction.inputs.empty() &&
                instruction.inputs.front().kind == ir::ValueKind::ADDRESS)
            {
                std::string target = valueToExpr(instruction.inputs.front(), context);
                std::string sourcePc = controlFlowSourcePcExpr(instruction);
                emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
                emitter.openBlock("if (!callIntrinsic(context.system, " + target +
                                  ", context.regs))");
                emitter.openBlock("if (!jumpRecompiledFunction(context, " + target + "))");
                emitter.writeLine("failUnsupportedJump(" + target + ", " + sourcePc + ");");
                emitter.closeBlock();
                emitter.closeBlock();
                emitter.writeLine("return true;");
            }
            else
            {
                emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
                emitter.writeLine("previousBlock = block;");
                emitter.writeLine("block = " + successor + ";");
                emitter.writeLine("continue;");
            }
        }
        return true;
    case ir::Opcode::CALL:
        if (!instruction.inputs.empty())
        {
            std::string target = valueToExpr(instruction.inputs.front(), context);
            std::string sourcePc = controlFlowSourcePcExpr(instruction);
            emitter.writeLine("traceInterestingCallsite(context, " + target + ", " + sourcePc +
                              ", false);");
            emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
            emitter.openBlock("if (!callIntrinsic(context.system, " + target + ", context.regs))");
            emitter.openBlock("if (!callRecompiledFunction(context, " + target + "))");
            emitter.writeLine("failUnsupportedCall(context, " + target + ", " + sourcePc + ");");
            emitter.closeBlock();
            emitter.closeBlock();
            emitter.writeLine("traceInterestingCallsite(context, " + target + ", " + sourcePc +
                              ", true);");
        }
        else
        {
            throwLoweringError(instruction, "Call instruction is missing a target operand");
        }
        return true;
    default:
        return false;
    }
}

} // namespace recompiler
} // namespace psxrecomp
