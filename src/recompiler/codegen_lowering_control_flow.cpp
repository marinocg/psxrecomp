#include "codegen_lowering_helpers.h"

#include <optional>
#include <sstream>

namespace psxrecomp
{
namespace recompiler
{

bool emitControlFlowInstruction(const ir::Instruction& instruction, const ir::BasicBlock& block,
                                const std::unordered_map<std::string, std::string>& blockNames,
                                LoweringContext& context, CppEmitter& emitter)
{
    switch (instruction.opcode)
    {
    case ir::Opcode::BRANCH:
        if (!instruction.inputs.empty())
        {
            std::string cond = valueToExpr(instruction.inputs.front(), context);
            std::string sourcePc = "0";
            if (instruction.sourceAddress.has_value())
            {
                std::ostringstream sourceStream;
                sourceStream << "0x" << std::hex << instruction.sourceAddress.value();
                sourcePc = sourceStream.str();
            }
            std::optional<std::string> takenTargetLiteral;
            if (instruction.inputs.size() >= 2 && instruction.inputs[1].kind == ir::ValueKind::ADDRESS)
            {
                std::ostringstream targetStream;
                targetStream << "0x" << std::hex << (instruction.inputs[1].address & 0x1FFFFFFFu);
                takenTargetLiteral = targetStream.str();
            }

            auto emitSuccessorTransfer = [&](const std::string& successorName,
                                             const std::optional<std::string>& externalTarget)
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
                    emitter.writeLine("failUnsupportedJump(" + *externalTarget + ", " + sourcePc +
                                      ");");
                    emitter.closeBlock();
                    emitter.writeLine("return;");
                    return;
                }

                // Branch targets can leave the current emitted function when
                // the CFG is partitioned. Dispatch through jumpRecompiledFunction
                // so execution continues at the correct absolute address.
                if (successorName.size() > 8 && successorName.substr(0, 8) == "block_0x")
                {
                    const Address targetAddress =
                        std::stoul(successorName.substr(6), nullptr, 16) & 0x1FFFFFFFu;
                    std::ostringstream targetStream;
                    targetStream << "0x" << std::hex << targetAddress;
                    const std::string targetLiteral = targetStream.str();
                    emitter.openBlock("if (!jumpRecompiledFunction(context, " + targetLiteral + "))");
                    emitter.writeLine("failUnsupportedJump(" + targetLiteral + ", " + sourcePc +
                                      ");");
                    emitter.closeBlock();
                    emitter.writeLine("return;");
                    return;
                }

                emitter.writeLine("return;");
            };

            // Detect self-loop spin-waits: on PSX, BEQ $zero,$zero,self is an
            // IRQ-breakable spin-wait. The IRQ handler modifies the return
            // address so execution resumes at the next instruction. In the
            // recompiled code there is no real interrupt mechanism, so we
            // replace the self-loop with an advanceFrame() call and fall
            // through to the next sequential block.
            auto isSelfLoop = [&](const std::string& successorName) -> bool
            { return successorName == block.name; };

            if (block.successors.size() >= 2)
            {
                bool takenIsSelf = isSelfLoop(block.successors[0]);
                bool fallthroughIsSelf = isSelfLoop(block.successors[1]);

                if (takenIsSelf && fallthroughIsSelf)
                {
                    emitter.writeLine("context.system.advanceFrame();");
                }
                else
                {
                    emitter.openBlock("if (" + cond + ")");
                    emitSuccessorTransfer(block.successors[0], takenTargetLiteral);
                    emitter.closeBlock();
                    emitter.openBlock("else");
                    emitSuccessorTransfer(block.successors[1], std::nullopt);
                    emitter.closeBlock();
                }
            }
            else if (block.successors.size() == 1)
            {
                if (isSelfLoop(block.successors[0]))
                {
                    emitter.writeLine("context.system.advanceFrame();");
                }
                else
                {
                    emitter.openBlock("if (" + cond + ")");
                    emitSuccessorTransfer(block.successors[0], takenTargetLiteral);
                    emitter.closeBlock();
                    emitter.openBlock("else");
                    emitter.writeLine("return;");
                    emitter.closeBlock();
                }
            }
        }
        return true;
    case ir::Opcode::JUMP:
        if (!instruction.inputs.empty() && instruction.inputs.front().kind == ir::ValueKind::REGISTER)
        {
            std::string target = valueToExpr(instruction.inputs.front(), context);
            std::string sourcePc = "0";
            if (instruction.sourceAddress.has_value())
            {
                std::ostringstream sourceStream;
                sourceStream << "0x" << std::hex << instruction.sourceAddress.value();
                sourcePc = sourceStream.str();
            }
            emitter.writeLine("const Address jumpTargetPhysical = " + target + " & 0x1FFFFFFF;");
            emitter.openBlock("if (jumpTargetPhysical == 0)");
            emitter.writeLine("return;");
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

            emitter.openBlock("if (!callIntrinsic(context.system, " + target + ", context.regs))");
            emitter.openBlock("if (!jumpRecompiledFunction(context, " + target + "))");
            emitter.writeLine("failUnsupportedJump(" + target + ", " + sourcePc + ");");
            emitter.closeBlock();
            emitter.closeBlock();
            emitter.writeLine("return;");
        }
        else if (!block.successors.empty())
        {
            const std::string successor = resolveBlockId(block.successors.front(), blockNames);
            if (successor.find("block_external") != std::string::npos && !instruction.inputs.empty() &&
                instruction.inputs.front().kind == ir::ValueKind::ADDRESS)
            {
                std::string target = valueToExpr(instruction.inputs.front(), context);
                std::string sourcePc = "0";
                if (instruction.sourceAddress.has_value())
                {
                    std::ostringstream sourceStream;
                    sourceStream << "0x" << std::hex << instruction.sourceAddress.value();
                    sourcePc = sourceStream.str();
                }
                emitter.openBlock("if (!callIntrinsic(context.system, " + target + ", context.regs))");
                emitter.openBlock("if (!callRecompiledFunction(context, " + target + "))");
                emitter.writeLine("failUnsupportedCall(" + target + ", " + sourcePc + ");");
                emitter.closeBlock();
                emitter.closeBlock();
                emitter.writeLine("return;");
            }
            else
            {
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
            std::string sourcePc = "0";
            if (instruction.sourceAddress.has_value())
            {
                std::ostringstream sourceStream;
                sourceStream << "0x" << std::hex << instruction.sourceAddress.value();
                sourcePc = sourceStream.str();
            }
            emitter.openBlock("if (!callIntrinsic(context.system, " + target + ", context.regs))");
            emitter.openBlock("if (!callRecompiledFunction(context, " + target + "))");
            emitter.writeLine("failUnsupportedCall(" + target + ", " + sourcePc + ");");
            emitter.closeBlock();
            emitter.closeBlock();
        }
        else
        {
            emitter.writeLine("// TODO: call lowering");
        }
        return true;
    default:
        return false;
    }
}

} // namespace recompiler
} // namespace psxrecomp
