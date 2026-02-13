#include "psxrecomp/recompiler/codegen.h"

#include "codegen_helpers.h"
#include "codegen_lowering_helpers.h"

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
            for (const auto& instruction : block.instructions)
            {
                if (instruction.opcode == ir::Opcode::PHI)
                {
                    continue;
                }
                emitInstruction(instruction, block, blockNames, context, emitter);
            }
            if (block.instructions.empty() ||
                block.instructions.back().opcode != ir::Opcode::RETURN)
            {
                if (!block.successors.empty())
                {
                    emitter.writeLine("previousBlock = block;");
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
