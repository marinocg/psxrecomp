#include "psxrecomp/recompiler/codegen.h"

#include "codegen_helpers.h"
#include "codegen_lowering_helpers.h"

#include <algorithm>
#include <cctype>
#include <optional>
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
        emitter.writeLine("bool " + functionName +
                          "(RecompilerContext& context, Address startAddress)");
        emitter.openBlock("");
        if (function.blocks.empty())
        {
            throw std::runtime_error("Cannot lower function '" + function.name +
                                     "' because it has no basic blocks.");
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
        std::vector<std::pair<Address, std::string>> blockStartDispatchEntries;
        std::vector<Address> resumableAddresses;
        std::vector<std::pair<Address, Address>> resumableAddressRanges;
        blockIds.reserve(function.blocks.size());
        blockStartDispatchEntries.reserve(function.blocks.size());
        resumableAddresses.reserve(function.blocks.size());
        std::unordered_set<Address> seenBlockStarts;
        std::unordered_set<Address> seenResumableAddresses;
        auto instructionGprWriteMask = [](const ir::Instruction& instruction) -> u32
        {
            u32 mask = 0;
            for (const auto& output : instruction.outputs)
            {
                if (output.kind != ir::ValueKind::REGISTER || output.reg == Registers::ZERO ||
                    output.reg >= Registers::NUM_REGISTERS)
                {
                    continue;
                }
                mask |= 1u << static_cast<u32>(output.reg);
            }
            return mask;
        };
        auto parseBlockStartAddress = [](const std::string& blockName) -> std::optional<Address>
        {
            static constexpr const char* kPrefix = "block_0x";
            static constexpr size_t kPrefixSize = 8;
            if (blockName.compare(0, kPrefixSize, kPrefix) != 0)
            {
                return std::nullopt;
            }

            const size_t hexStart = kPrefixSize;
            if (hexStart >= blockName.size())
            {
                return std::nullopt;
            }
            for (size_t index = hexStart; index < blockName.size(); ++index)
            {
                if (!std::isxdigit(static_cast<unsigned char>(blockName[index])))
                {
                    return std::nullopt;
                }
            }

            std::istringstream parser(blockName.substr(hexStart));
            Address parsed = 0;
            parser >> std::hex >> parsed;
            if (parser.fail())
            {
                return std::nullopt;
            }
            return parsed & 0x1FFFFFFFu;
        };
        for (const auto& block : function.blocks)
        {
            std::string uniqueName = uniquifyIdentifier(block.name, usedBlocks);
            if (blockNames.find(block.name) == blockNames.end())
            {
                blockNames.emplace(block.name, uniqueName);
            }
            blockIds.push_back(uniqueName);

            std::optional<Address> blockStart = parseBlockStartAddress(block.name);
            if (!blockStart.has_value())
            {
                for (const auto& instruction : block.instructions)
                {
                    if (instruction.sourceAddress.has_value())
                    {
                        blockStart = (*instruction.sourceAddress) & 0x1FFFFFFFu;
                        break;
                    }
                }
            }

            if (blockStart.has_value() && seenBlockStarts.insert(*blockStart).second)
            {
                blockStartDispatchEntries.emplace_back(*blockStart, uniqueName);
            }
            for (const auto& instruction : block.instructions)
            {
                if (!instruction.sourceAddress.has_value())
                {
                    continue;
                }
                const Address instructionAddress = (*instruction.sourceAddress) & 0x1FFFFFFFu;
                if (seenResumableAddresses.insert(instructionAddress).second)
                {
                    resumableAddresses.push_back(instructionAddress);
                }
            }
        }
        std::sort(blockStartDispatchEntries.begin(), blockStartDispatchEntries.end(),
                  [](const std::pair<Address, std::string>& left,
                     const std::pair<Address, std::string>& right)
                  { return left.first < right.first; });
        std::sort(resumableAddresses.begin(), resumableAddresses.end());
        for (Address address : resumableAddresses)
        {
            if ((address & 0x3u) != 0)
            {
                continue;
            }

            if (resumableAddressRanges.empty() || resumableAddressRanges.back().second != address)
            {
                resumableAddressRanges.emplace_back(address, address + 4);
                continue;
            }

            resumableAddressRanges.back().second = address + 4;
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
        emitter.writeLine("Address resumeAddress = 0;");

        // Emit a startAddress → BlockId dispatch so that callers can enter
        // this function at an arbitrary block (needed for JALR targets that
        // point into the middle of a function).
        if (!function.blocks.empty())
        {
            emitter.writeLine("if (startAddress != 0)");
            emitter.openBlock("");
            emitter.writeLine("Address physical = startAddress & 0x1FFFFFFF;");
            if (resumableAddressRanges.empty())
            {
                emitter.writeLine("return false;");
                emitter.closeBlock();
            }
            else
            {
                emitter.writeLine("static constexpr Address kResumeRangeStarts[] = {");
                for (const auto& range : resumableAddressRanges)
                {
                    std::ostringstream line;
                    line << "    0x" << std::hex << range.first << ",";
                    emitter.writeLine(line.str());
                }
                emitter.writeLine("};");
                emitter.writeLine("static constexpr Address kResumeRangeEnds[] = {");
                for (const auto& range : resumableAddressRanges)
                {
                    std::ostringstream line;
                    line << "    0x" << std::hex << range.second << ",";
                    emitter.writeLine(line.str());
                }
                emitter.writeLine("};");
                emitter.writeLine("const Address* rangeStartsBegin = kResumeRangeStarts;");
                emitter.writeLine("const Address* rangeStartsEnd = kResumeRangeStarts + "
                                  "sizeof(kResumeRangeStarts) / sizeof(kResumeRangeStarts[0]);");
                emitter.writeLine("const Address* rangeIt = std::upper_bound(rangeStartsBegin, "
                                  "rangeStartsEnd, physical);");
                emitter.writeLine("if (rangeIt == rangeStartsBegin)");
                emitter.openBlock("");
                emitter.writeLine("return false;");
                emitter.closeBlock();
                emitter.writeLine(
                    "const size_t rangeIdx = static_cast<size_t>((rangeIt - rangeStartsBegin) - "
                    "1);");
                emitter.writeLine(
                    "if ((physical & 0x3u) != 0 || physical >= kResumeRangeEnds[rangeIdx])");
                emitter.openBlock("");
                emitter.writeLine("return false;");
                emitter.closeBlock();

                if (blockStartDispatchEntries.empty())
                {
                    emitter.writeLine("return false;");
                    emitter.closeBlock();
                }
                else
                {
                    emitter.writeLine("static constexpr Address kBlockStarts[] = {");
                    {
                        for (const auto& entry : blockStartDispatchEntries)
                        {
                            std::ostringstream line;
                            line << "    0x" << std::hex << entry.first << ",";
                            emitter.writeLine(line.str());
                        }
                    }
                    emitter.writeLine("};");
                    emitter.writeLine("static constexpr BlockId kBlockIds[] = {");
                    for (const auto& entry : blockStartDispatchEntries)
                    {
                        emitter.writeLine("    BlockId::" + entry.second + ",");
                    }
                    emitter.writeLine("};");
                    emitter.writeLine("const Address* startsBegin = kBlockStarts;");
                    emitter.writeLine("const Address* startsEnd = kBlockStarts + "
                                      "sizeof(kBlockStarts) / sizeof(kBlockStarts[0]);");
                    emitter.writeLine(
                        "const Address* it = std::upper_bound(startsBegin, startsEnd, physical);");
                    emitter.writeLine("if (it == startsBegin)");
                    emitter.openBlock("");
                    emitter.writeLine("return false;");
                    emitter.closeBlock();
                    emitter.writeLine(
                        "const size_t idx = static_cast<size_t>((it - startsBegin) - 1);");
                    emitter.writeLine("block = kBlockIds[idx];");
                    emitter.writeLine("resumeAddress = physical;");
                    emitter.writeLine("context.system.setLastResumeAddress(physical);");
                    emitter.closeBlock();
                }
            }
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
                // Fallback: unknown predecessor resumes are treated as a
                // normal function exit.
                emitter.writeLine("return true;");
                emitter.closeBlock();
                continue;
            }

            // Resume-safe instruction emission: group all IR instructions
            // belonging to the same MIPS source address under a single resume
            // guard.  Instructions without a source address (continuation ops
            // of the preceding MIPS instruction) are kept inside the current
            // guard so they are correctly skipped when resuming past that
            // source address.
            bool inResumeGuard = false;
            std::optional<Address> currentGuardAddress;
            u32 currentGuardWriteMask = 0;
            emitter.writeLine("u32 instructionGroupWriteMask = 0;");
            for (const auto& instruction : block.instructions)
            {
                if (instruction.opcode == ir::Opcode::PHI)
                {
                    continue;
                }
                if (instruction.sourceAddress.has_value())
                {
                    const Address physical = (*instruction.sourceAddress) & 0x1FFFFFFFu;
                    // Open a new guard only when the source address changes
                    // (or no guard is open yet).  This coalesces all IR
                    // instructions from the same MIPS instruction into one
                    // resume-guard block, preventing stale-temporary reads
                    // when the block is entered via a mid-block resume.
                    if (!inResumeGuard || !currentGuardAddress.has_value() ||
                        *currentGuardAddress != physical)
                    {
                        if (inResumeGuard)
                        {
                            std::ostringstream maskLiteral;
                            maskLiteral << "0x" << std::hex << currentGuardWriteMask << "u";
                            emitter.writeLine("finishLoadDelayCycle(context, " + maskLiteral.str() +
                                              ");");
                            emitter.closeBlock();
                        }
                        std::ostringstream addrLiteral;
                        addrLiteral << "0x" << std::hex << physical;
                        emitter.openBlock("if (resumeAddress == 0 || resumeAddress == " +
                                          addrLiteral.str() + ")");
                        emitter.openBlock("if (resumeAddress != 0)");
                        emitter.writeLine("resumeAddress = 0;");
                        emitter.writeLine("context.system.setLastResumeAddress(0);");
                        emitter.closeBlock();
                        std::ostringstream pcLine;
                        pcLine << "setProgramCounter(context, 0x" << std::hex << physical << ");";
                        emitter.writeLine(pcLine.str());
                        emitter.writeLine("instructionGroupWriteMask = 0;");
                        currentGuardAddress = physical;
                        inResumeGuard = true;
                        currentGuardWriteMask = 0;
                    }
                    currentGuardWriteMask |= instructionGprWriteMask(instruction);
                    {
                        std::ostringstream maskLiteral;
                        maskLiteral << "0x" << std::hex << currentGuardWriteMask << "u";
                        emitter.writeLine("instructionGroupWriteMask = " + maskLiteral.str() +
                                          ";");
                    }
                    emitInstruction(instruction, block, blockNames, context, emitter);
                    continue;
                }
                // Instructions without a source address belong to the
                // preceding MIPS instruction group.  They stay inside the
                // current resume guard so they are correctly skipped when
                // execution is resumed past their owning source address.
                currentGuardWriteMask |= instructionGprWriteMask(instruction);
                {
                    std::ostringstream maskLiteral;
                    maskLiteral << "0x" << std::hex << currentGuardWriteMask << "u";
                    emitter.writeLine("instructionGroupWriteMask = " + maskLiteral.str() + ";");
                }
                emitInstruction(instruction, block, blockNames, context, emitter);
            }
            if (inResumeGuard)
            {
                emitter.writeLine("finishLoadDelayCycle(context, instructionGroupWriteMask);");
                emitter.closeBlock();
            }
            if (block.instructions.empty() ||
                block.instructions.back().opcode != ir::Opcode::RETURN)
            {
                if (!block.successors.empty())
                {
                    std::string resolvedSuccessor =
                        resolveBlockId(block.successors.front(), blockNames);
                    std::string currentBlockId = "BlockId::" + blockIds[blockIndex];

                    emitter.writeLine("previousBlock = block;");
                    emitter.writeLine("block = " + resolvedSuccessor + ";");
                    emitter.writeLine("continue;");
                }
                else
                {
                    emitter.writeLine("return true;");
                }
            }
            emitter.closeBlock();
        }
        emitter.writeLine("default:");
        emitter.openBlock("");
        emitter.writeLine("return false;");
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
