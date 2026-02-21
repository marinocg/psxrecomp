#include "psxrecomp/recompiler/codegen.h"

#include "codegen_helpers.h"
#include "codegen_lowering_helpers.h"

#include <sstream>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{

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
        emitter.writeLine("void " + functionName +
                          "(RecompilerContext& context, Address startAddress)");
        emitter.openBlock("");
        if (function.blocks.empty())
        {
            emitter.writeLine("// TODO: empty function body");
            emitter.writeLine("return;");
            emitter.closeBlock();
            emitter.writeBlank();
            continue;
        }

        emitter.writeLine("CycleScope cycleScope(context);");

        auto temporaries = collectTemporaries(function);
        for (u32 temporaryId : temporaries)
        {
            context.temporaries[temporaryId] = "temp" + std::to_string(temporaryId);
            emitter.writeLine("u32 " + context.temporaries[temporaryId] + " = 0;");
        }

        emitter.writeBlank();
        std::unordered_set<std::string> usedBlocks;
        std::unordered_map<std::string, std::string> blockNames;
        std::vector<std::string> blockIds;
        blockIds.reserve(function.blocks.size());
        for (const auto& block : function.blocks)
        {
            std::string uniqueName = uniquifyIdentifier(block.name, usedBlocks);
            if (blockNames.find(block.name) == blockNames.end())
            {
                blockNames.emplace(block.name, uniqueName);
            }
            blockIds.push_back(uniqueName);
        }

        emitter.writeLine("enum class BlockId {");
        for (size_t index = 0; index < blockIds.size(); ++index)
        {
            emitter.writeLine("    " + blockIds[index] + (index + 1 < blockIds.size() ? "," : ""));
        }
        emitter.writeLine("};");
        if (!blockIds.empty())
        {
            emitter.writeLine("BlockId block = BlockId::" + blockIds.front() + ";");
        }

        // Emit a startAddress → BlockId dispatch so that callers can enter
        // this function at an arbitrary block (needed for JALR targets that
        // point into the middle of a function).
        if (function.blocks.size() > 1)
        {
            emitter.writeLine("if (startAddress != 0)");
            emitter.openBlock("");
            emitter.writeLine("Address physical = startAddress & 0x1FFFFFFF;");
            emitter.writeLine("switch (physical)");
            emitter.openBlock("");
            for (size_t i = 0; i < function.blocks.size(); ++i)
            {
                const auto& blockName = function.blocks[i].name;
                // Block names are formatted as "block_0x<hex_addr>"
                if (blockName.size() > 8 && blockName.substr(0, 8) == "block_0x")
                {
                    const Address blockAddr =
                        std::stoul(blockName.substr(6), nullptr, 16) & 0x1FFFFFFFu;
                    std::ostringstream caseLine;
                    caseLine << "case 0x" << std::hex << blockAddr
                             << ": block = BlockId::" << blockIds[i] << "; break;";
                    emitter.writeLine(caseLine.str());
                }
            }
            emitter.writeLine("default: break;");
            emitter.closeBlock();
            emitter.closeBlock();
        }

        emitter.writeLine("BlockId previousBlock = block;");
        const auto indexMap = buildBlockIndex(function);
        const auto predecessors = buildPredecessors(function, indexMap);
        emitter.writeLine("while (true)");
        emitter.openBlock("");
        emitter.writeLine("switch (block)");
        emitter.openBlock("");
        for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
        {
            const auto& block = function.blocks[blockIndex];
            emitter.writeLine("case BlockId::" + blockIds[blockIndex] + ":");
            emitter.openBlock("");
            emitPhiAssignments(block, predecessors[blockIndex], blockNames, context, emitter);

            // If this is the external barrier block, emit continuation dispatch
            // instead of a plain return so execution resumes at the right block.
            if (!block.continuations.empty())
            {
                bool first = true;
                for (const auto& entry : block.continuations)
                {
                    std::string condition =
                        "previousBlock == " + resolveBlockId(entry.first, blockNames);
                    if (first)
                    {
                        emitter.openBlock("if (" + condition + ")");
                        first = false;
                    }
                    else
                    {
                        emitter.openBlock("else if (" + condition + ")");
                    }
                    emitter.writeLine("block = " + resolveBlockId(entry.second, blockNames) + ";");
                    emitter.writeLine("continue;");
                    emitter.closeBlock();
                }
                // Fallback: if no continuation matches, return.
                emitter.writeLine("return;");
                emitter.closeBlock();
                continue;
            }

            for (const auto& instruction : block.instructions)
            {
                if (instruction.opcode == ir::Opcode::PHI)
                {
                    continue;
                }
                if (instruction.sourceAddress.has_value())
                {
                    std::ostringstream pcLine;
                    pcLine << "setProgramCounter(context, 0x" << std::hex
                           << ((*instruction.sourceAddress) & 0x1FFFFFFFu) << ");";
                    emitter.writeLine(pcLine.str());
                }
                emitInstruction(instruction, block, blockNames, context, emitter);
            }
            if (block.instructions.empty() ||
                block.instructions.back().opcode != ir::Opcode::RETURN)
            {
                if (!block.successors.empty())
                {
                    std::string resolvedSuccessor =
                        resolveBlockId(block.successors.front(), blockNames);
                    std::string currentBlockId = "BlockId::" + blockIds[blockIndex];

                    // Detect self-loop: if the resolved successor is the current
                    // block but a continuation block exists (next in sequence),
                    // redirect to the continuation to prevent infinite self-loops.
                    if (resolvedSuccessor == currentBlockId &&
                        blockIndex + 1 < function.blocks.size())
                    {
                        resolvedSuccessor = "BlockId::" + blockIds[blockIndex + 1];
                    }

                    emitter.writeLine("previousBlock = block;");
                    emitter.writeLine("block = " + resolvedSuccessor + ";");
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
