#include "psxrecomp/ir/verify.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace ir
{

namespace
{
std::unordered_map<std::string, size_t> buildBlockIndex(const Function& function)
{
    std::unordered_map<std::string, size_t> indexMap;
    for (size_t index = 0; index < function.blocks.size(); ++index)
    {
        indexMap[function.blocks[index].name] = index;
    }
    return indexMap;
}

std::vector<std::vector<size_t>> buildPredecessors(const Function& function,
                                                   const std::unordered_map<std::string, size_t>& indexMap)
{
    std::vector<std::vector<size_t>> predecessors(function.blocks.size());
    for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
    {
        const auto& block = function.blocks[blockIndex];
        for (const auto& successor : block.successors)
        {
            auto it = indexMap.find(successor);
            if (it != indexMap.end())
            {
                predecessors[it->second].push_back(blockIndex);
            }
        }
    }
    return predecessors;
}

std::vector<std::vector<bool>> computeDominators(const Function& function,
                                                 const std::vector<std::vector<size_t>>& predecessors)
{
    const size_t blockCount = function.blocks.size();
    std::vector<std::vector<bool>> dominators(blockCount, std::vector<bool>(blockCount, true));

    if (blockCount == 0)
    {
        return dominators;
    }

    for (size_t index = 0; index < blockCount; ++index)
    {
        if (index == 0)
        {
            std::fill(dominators[index].begin(), dominators[index].end(), false);
            dominators[index][index] = true;
            continue;
        }
        if (predecessors[index].empty())
        {
            std::fill(dominators[index].begin(), dominators[index].end(), false);
            dominators[index][index] = true;
        }
    }

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t blockIndex = 1; blockIndex < blockCount; ++blockIndex)
        {
            if (predecessors[blockIndex].empty())
            {
                continue;
            }
            std::vector<bool> newDoms(blockCount, true);
            for (size_t predIndex : predecessors[blockIndex])
            {
                for (size_t domIndex = 0; domIndex < blockCount; ++domIndex)
                {
                    newDoms[domIndex] = newDoms[domIndex] && dominators[predIndex][domIndex];
                }
            }
            newDoms[blockIndex] = true;
            if (newDoms != dominators[blockIndex])
            {
                dominators[blockIndex] = std::move(newDoms);
                changed = true;
            }
        }
    }

    return dominators;
}

} // namespace

VerificationResult verifyFunction(const Function& function)
{
    VerificationResult result;
    if (function.blocks.empty())
    {
        result.errors.push_back("Function contains no basic blocks.");
        return result;
    }

    auto indexMap = buildBlockIndex(function);
    auto predecessors = buildPredecessors(function, indexMap);

    for (const auto& block : function.blocks)
    {
        for (const auto& successor : block.successors)
        {
            if (indexMap.find(successor) == indexMap.end())
            {
                result.errors.push_back("Unknown successor block: " + successor);
            }
        }
    }

    std::unordered_map<u32, size_t> tempDefinitions;

    for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
    {
        const auto& block = function.blocks[blockIndex];
        bool nonPhiSeen = false;
        for (const auto& instruction : block.instructions)
        {
            if (instruction.opcode != Opcode::PHI)
            {
                nonPhiSeen = true;
            }
            else if (nonPhiSeen)
            {
                result.errors.push_back("Phi instruction appears after non-phi in block " +
                                        block.name);
            }

            for (const auto& output : instruction.outputs)
            {
                if (output.kind == ValueKind::TEMPORARY)
                {
                    if (tempDefinitions.find(output.temporaryId) != tempDefinitions.end())
                    {
                        result.errors.push_back("Temporary defined multiple times: t" +
                                                std::to_string(output.temporaryId));
                    }
                    tempDefinitions[output.temporaryId] = blockIndex;
                }
            }
        }
    }

    auto dominators = computeDominators(function, predecessors);

    for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
    {
        const auto& block = function.blocks[blockIndex];
        size_t predecessorCount = predecessors[blockIndex].size();
        for (const auto& instruction : block.instructions)
        {
            if (instruction.opcode == Opcode::PHI)
            {
                if (instruction.inputs.size() != predecessorCount)
                {
                    result.errors.push_back("Phi input count mismatch in block " + block.name);
                }
                for (size_t predIndex = 0; predIndex < instruction.inputs.size(); ++predIndex)
                {
                    const auto& input = instruction.inputs[predIndex];
                    if (input.kind == ValueKind::TEMPORARY)
                    {
                        auto defIt = tempDefinitions.find(input.temporaryId);
                        if (defIt != tempDefinitions.end())
                        {
                            size_t defBlock = defIt->second;
                            size_t predBlock = predecessors[blockIndex][predIndex];
                            if (!dominators[predBlock][defBlock])
                            {
                                result.errors.push_back("Phi input not dominated by definition in " +
                                                        block.name);
                            }
                        }
                    }
                }
                continue;
            }

            for (const auto& input : instruction.inputs)
            {
                if (input.kind == ValueKind::TEMPORARY)
                {
                    auto defIt = tempDefinitions.find(input.temporaryId);
                    if (defIt == tempDefinitions.end())
                    {
                        result.errors.push_back("Use of undefined temporary t" +
                                                std::to_string(input.temporaryId));
                        continue;
                    }
                    size_t defBlock = defIt->second;
                    if (!dominators[blockIndex][defBlock])
                    {
                        result.errors.push_back("Use of temporary before dominance in block " +
                                                block.name);
                    }
                }
            }
        }
    }

    return result;
}

} // namespace ir
} // namespace psxrecomp
