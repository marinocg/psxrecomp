#include "psxrecomp/disasm/analysis.h"

#include "analysis_helpers.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace disasm
{

namespace
{
constexpr Register kReturnAddress = detail::kReturnAddress;
} // namespace

std::vector<FunctionBoundary> findFunctionBoundaries(const std::vector<Instruction>& instructions)
{
    std::vector<FunctionBoundary> boundaries;
    if (instructions.empty())
    {
        return boundaries;
    }

    std::unordered_set<Address> startAddresses;
    startAddresses.insert(instructions.front().address);

    for (size_t i = 0; i < instructions.size(); ++i)
    {
        if (detail::hasProloguePattern(instructions, i))
        {
            startAddresses.insert(instructions[i].address);
        }

        if (detail::isDirectCall(instructions[i]))
        {
            if (auto target = detail::resolveDirectCallTarget(instructions[i]))
            {
                startAddresses.insert(*target);
            }
        }
    }

    std::vector<Address> sortedStarts(startAddresses.begin(), startAddresses.end());
    std::sort(sortedStarts.begin(), sortedStarts.end());

    auto indexMap = detail::buildInstructionIndex(instructions);
    for (size_t startIndex = 0; startIndex < sortedStarts.size(); ++startIndex)
    {
        const Address start = sortedStarts[startIndex];
        auto it = indexMap.find(start);
        if (it == indexMap.end())
        {
            continue;
        }

        const size_t startInstructionIndex = it->second;
        const size_t limitIndex =
            (startIndex + 1 < sortedStarts.size() && indexMap.count(sortedStarts[startIndex + 1]))
                ? indexMap[sortedStarts[startIndex + 1]]
                : instructions.size();

        Address end = instructions[limitIndex - 1].address;
        bool hasEpilogue = false;

        for (size_t i = startInstructionIndex; i < limitIndex; ++i)
        {
            if (instructions[i].isReturn())
            {
                size_t endIndex = i;
                if (instructions[i].hasDelaySlot() && i + 1 < instructions.size())
                {
                    endIndex = i + 1;
                }
                end = instructions[endIndex].address;
                hasEpilogue = detail::hasEpiloguePattern(instructions, i);
                break;
            }
        }

        boundaries.push_back({start, end,
                              detail::hasProloguePattern(instructions, startInstructionIndex),
                              hasEpilogue});
    }

    return boundaries;
}

std::vector<IndirectBranchTarget>
findIndirectBranchTargets(const std::vector<Instruction>& instructions)
{
    std::vector<IndirectBranchTarget> targets;
    targets.reserve(instructions.size());
    for (const auto& instruction : instructions)
    {
        if (instruction.opcode != Opcode::JR && instruction.opcode != Opcode::JALR)
        {
            continue;
        }

        if (instruction.rs == kReturnAddress)
        {
            continue;
        }

        targets.push_back(
            {instruction.address, instruction.rs, instruction.opcode == Opcode::JALR});
    }

    return targets;
}

std::vector<JumpTableInfo> findJumpTables(const std::vector<Instruction>& instructions)
{
    std::vector<JumpTableInfo> tables;
    if (instructions.empty())
    {
        return tables;
    }

    for (size_t i = 0; i < instructions.size(); ++i)
    {
        const Instruction& instruction = instructions[i];
        if (instruction.opcode != Opcode::JR && instruction.opcode != Opcode::JALR)
        {
            continue;
        }

        if (instruction.rs == kReturnAddress)
        {
            continue;
        }

        Register jumpRegister = instruction.rs;
        std::optional<size_t> loadIndex;
        for (size_t back = 1; back <= 4 && i >= back; ++back)
        {
            const Instruction& candidate = instructions[i - back];
            if (candidate.opcode == Opcode::LW && candidate.rt == jumpRegister)
            {
                loadIndex = i - back;
                break;
            }
        }

        if (!loadIndex)
        {
            continue;
        }

        const Instruction& loadInstruction = instructions[*loadIndex];
        Register tableRegister = loadInstruction.rs;
        s16 tableOffset = loadInstruction.immediate;

        std::optional<Register> indexRegister;
        for (size_t back = 1; back <= 4 && *loadIndex >= back; ++back)
        {
            const Instruction& candidate = instructions[*loadIndex - back];
            if ((candidate.opcode == Opcode::ADDU || candidate.opcode == Opcode::ADD) &&
                candidate.rd == tableRegister)
            {
                if (candidate.rs == tableRegister)
                {
                    indexRegister = candidate.rt;
                }
                else if (candidate.rt == tableRegister)
                {
                    indexRegister = candidate.rs;
                }
                break;
            }
        }

        auto tableBaseAddress =
            detail::resolveImmediateAddress(instructions, *loadIndex, tableRegister);

        tables.push_back({instruction.address, jumpRegister, tableRegister, indexRegister,
                          tableBaseAddress, tableOffset});
    }

    return tables;
}

CodeDataSegmentation segmentCodeAndData(const std::vector<Instruction>& instructions,
                                        const std::vector<Address>& entryPoints,
                                        const std::vector<JumpTableInfo>& jumpTables)
{
    CodeDataSegmentation segmentation;
    if (instructions.empty())
    {
        return segmentation;
    }

    auto indexMap = detail::buildInstructionIndex(instructions);
    std::unordered_set<Address> visited;
    std::queue<Address> worklist;

    auto visitIfValid = [&](Address address, bool enqueue)
    {
        if (indexMap.count(address) == 0)
        {
            return;
        }

        if (visited.insert(address).second && enqueue)
        {
            worklist.push(address);
        }
    };

    if (entryPoints.empty())
    {
        visitIfValid(instructions.front().address, true);
    }
    else
    {
        for (Address entry : entryPoints)
        {
            visitIfValid(entry, true);
        }
    }

    std::unordered_map<Address, JumpTableInfo> jumpTableMap;
    for (const auto& table : jumpTables)
    {
        jumpTableMap.emplace(table.jumpAddress, table);
    }

    while (!worklist.empty())
    {
        const Address address = worklist.front();
        worklist.pop();

        const size_t index = indexMap[address];
        const Instruction& instruction = instructions[index];

        const auto enqueueDelaySlot = [&](bool enqueue)
        {
            if (index + 1 < instructions.size())
            {
                visitIfValid(instructions[index + 1].address, enqueue);
            }
        };

        const auto enqueueAfterDelaySlot = [&]()
        {
            if (index + 2 < instructions.size())
            {
                visitIfValid(instructions[index + 2].address, true);
            }
        };

        if (instruction.isBranch())
        {
            if (auto target = instruction.getBranchTarget())
            {
                visitIfValid(*target, true);
            }

            enqueueDelaySlot(true);
            enqueueAfterDelaySlot();
            continue;
        }

        if (instruction.isJump())
        {
            if (instruction.hasDelaySlot())
            {
                const bool shouldEnqueueDelaySlot =
                    instruction.opcode == Opcode::JAL || instruction.opcode == Opcode::JALR;
                enqueueDelaySlot(shouldEnqueueDelaySlot);
            }

            if (instruction.opcode == Opcode::J || instruction.opcode == Opcode::JAL)
            {
                if (auto target = instruction.getJumpTarget())
                {
                    visitIfValid(*target, true);
                }
            }

            if (instruction.opcode == Opcode::JAL || instruction.opcode == Opcode::JALR)
            {
                enqueueAfterDelaySlot();
            }

            if (instruction.opcode == Opcode::JR && instruction.rs == kReturnAddress)
            {
                continue;
            }

            if (jumpTableMap.count(instruction.address) > 0)
            {
                continue;
            }

            continue;
        }

        if (index + 1 < instructions.size())
        {
            visitIfValid(instructions[index + 1].address, true);
        }
    }

    std::unordered_set<Address> codeAddresses = visited;
    std::unordered_set<Address> dataAddresses;
    dataAddresses.reserve(instructions.size());

    for (const auto& instruction : instructions)
    {
        if (codeAddresses.count(instruction.address) == 0)
        {
            dataAddresses.insert(instruction.address);
        }
    }

    segmentation.codeRanges = detail::buildRanges(instructions, codeAddresses);
    segmentation.dataRanges = detail::buildRanges(instructions, dataAddresses);

    return segmentation;
}

} // namespace disasm
} // namespace psxrecomp
