#include "psxrecomp/ir/ssa.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace ir
{

namespace
{
struct BlockDefInfo
{
    std::unordered_map<Register, u32> inDefs;
    std::unordered_map<Register, u32> outDefs;
};

u32 nextTemporaryId(const Function& function)
{
    u32 maxId = 0;
    bool found = false;
    for (const auto& block : function.blocks)
    {
        for (const auto& instruction : block.instructions)
        {
            for (const auto& value : instruction.inputs)
            {
                if (value.kind == ValueKind::TEMPORARY)
                {
                    maxId = std::max(maxId, value.temporaryId);
                    found = true;
                }
            }
            for (const auto& value : instruction.outputs)
            {
                if (value.kind == ValueKind::TEMPORARY)
                {
                    maxId = std::max(maxId, value.temporaryId);
                    found = true;
                }
            }
        }
    }
    return found ? maxId + 1 : 0;
}

std::unordered_map<std::string, size_t> buildBlockIndex(const Function& function)
{
    std::unordered_map<std::string, size_t> indexMap;
    for (size_t index = 0; index < function.blocks.size(); ++index)
    {
        indexMap[function.blocks[index].name] = index;
    }
    return indexMap;
}

std::vector<std::vector<size_t>>
buildPredecessors(const Function& function, const std::unordered_map<std::string, size_t>& indexMap)
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

} // namespace

SsaBuildResult convertToSSA(Function& function)
{
    SsaBuildResult result;
    if (function.blocks.empty())
    {
        result.errors.push_back("SSA conversion requires at least one basic block.");
        return result;
    }

    const auto indexMap = buildBlockIndex(function);
    const auto predecessors = buildPredecessors(function, indexMap);

    std::unordered_set<Register> registersUsed;
    std::unordered_set<Register> registersDefined;

    std::vector<std::vector<std::vector<std::optional<u32>>>> defIds(function.blocks.size());
    u32 nextDefId = 0;

    for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
    {
        const auto& block = function.blocks[blockIndex];
        defIds[blockIndex].resize(block.instructions.size());
        for (size_t instIndex = 0; instIndex < block.instructions.size(); ++instIndex)
        {
            const auto& instruction = block.instructions[instIndex];
            for (const auto& input : instruction.inputs)
            {
                if (input.kind == ValueKind::REGISTER)
                {
                    registersUsed.insert(input.reg);
                }
            }
            defIds[blockIndex][instIndex].resize(instruction.outputs.size());
            for (size_t outputIndex = 0; outputIndex < instruction.outputs.size(); ++outputIndex)
            {
                const auto& output = instruction.outputs[outputIndex];
                if (output.kind == ValueKind::REGISTER)
                {
                    defIds[blockIndex][instIndex][outputIndex] = nextDefId++;
                    registersDefined.insert(output.reg);
                }
            }
        }
    }

    std::vector<Register> orderedRegisters;
    orderedRegisters.reserve(registersUsed.size() + registersDefined.size());
    orderedRegisters.insert(orderedRegisters.end(), registersUsed.begin(), registersUsed.end());
    for (Register reg : registersDefined)
    {
        if (std::find(orderedRegisters.begin(), orderedRegisters.end(), reg) ==
            orderedRegisters.end())
        {
            orderedRegisters.push_back(reg);
        }
    }
    std::sort(orderedRegisters.begin(), orderedRegisters.end());

    std::unordered_map<Register, u32> entryParamDefs;
    for (Register reg : orderedRegisters)
    {
        entryParamDefs[reg] = nextDefId++;
    }

    std::vector<std::unordered_map<Register, u32>> phiDefs(function.blocks.size());
    std::vector<BlockDefInfo> blockDefs(function.blocks.size());

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
        {
            std::unordered_map<Register, u32> mergedIn;
            if (blockIndex == 0)
            {
                mergedIn = entryParamDefs;
            }
            else if (!predecessors[blockIndex].empty())
            {
                std::unordered_map<Register, u32> candidate;
                for (size_t predIndex : predecessors[blockIndex])
                {
                    for (const auto& [reg, defId] : blockDefs[predIndex].outDefs)
                    {
                        candidate[reg] = defId;
                    }
                }

                for (const auto& [reg, defId] : candidate)
                {
                    bool allSame = true;
                    u32 firstDef = defId;
                    for (size_t predIndex : predecessors[blockIndex])
                    {
                        auto it = blockDefs[predIndex].outDefs.find(reg);
                        if (it == blockDefs[predIndex].outDefs.end() || it->second != firstDef)
                        {
                            allSame = false;
                            break;
                        }
                    }

                    if (allSame)
                    {
                        mergedIn[reg] = firstDef;
                    }
                    else
                    {
                        auto phiIt = phiDefs[blockIndex].find(reg);
                        if (phiIt == phiDefs[blockIndex].end())
                        {
                            phiDefs[blockIndex][reg] = nextDefId++;
                        }
                        mergedIn[reg] = phiDefs[blockIndex][reg];
                    }
                }
            }

            if (mergedIn != blockDefs[blockIndex].inDefs)
            {
                blockDefs[blockIndex].inDefs = mergedIn;
                changed = true;
            }

            std::unordered_map<Register, u32> current = blockDefs[blockIndex].inDefs;
            const auto& block = function.blocks[blockIndex];
            for (size_t instIndex = 0; instIndex < block.instructions.size(); ++instIndex)
            {
                for (size_t outputIndex = 0;
                     outputIndex < block.instructions[instIndex].outputs.size(); ++outputIndex)
                {
                    const auto& output = block.instructions[instIndex].outputs[outputIndex];
                    if (output.kind == ValueKind::REGISTER)
                    {
                        current[output.reg] = *defIds[blockIndex][instIndex][outputIndex];
                    }
                }
            }

            if (current != blockDefs[blockIndex].outDefs)
            {
                blockDefs[blockIndex].outDefs = current;
                changed = true;
            }
        }
    }

    u32 nextTempId = nextTemporaryId(function);
    std::vector<Value> defIdToValue(nextDefId, Value::invalid());

    auto assignTemp = [&](u32 defId)
    {
        if (defIdToValue[defId].kind == ValueKind::INVALID)
        {
            defIdToValue[defId] = Value::makeTemporary(nextTempId++);
        }
        return defIdToValue[defId];
    };

    for (auto& [reg, defId] : entryParamDefs)
    {
        assignTemp(defId);
    }
    for (auto& phiMap : phiDefs)
    {
        for (auto& [reg, defId] : phiMap)
        {
            assignTemp(defId);
        }
    }
    for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
    {
        for (size_t instIndex = 0; instIndex < defIds[blockIndex].size(); ++instIndex)
        {
            for (const auto& defIdOpt : defIds[blockIndex][instIndex])
            {
                if (defIdOpt.has_value())
                {
                    assignTemp(*defIdOpt);
                }
            }
        }
    }

    for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
    {
        auto& block = function.blocks[blockIndex];
        std::vector<Instruction> newInstructions;
        auto current = blockDefs[blockIndex].inDefs;

        if (blockIndex == 0 && !entryParamDefs.empty())
        {
            for (const auto& [reg, defId] : entryParamDefs)
            {
                Instruction phiInstruction{Opcode::PHI, {}, {assignTemp(defId)}, std::nullopt};
                newInstructions.push_back(std::move(phiInstruction));
                current[reg] = defId;
            }
        }

        if (!phiDefs[blockIndex].empty())
        {
            for (const auto& [reg, defId] : phiDefs[blockIndex])
            {
                std::vector<Value> inputs;
                inputs.reserve(predecessors[blockIndex].size());
                for (size_t predIndex : predecessors[blockIndex])
                {
                    auto outIt = blockDefs[predIndex].outDefs.find(reg);
                    if (outIt != blockDefs[predIndex].outDefs.end())
                    {
                        inputs.push_back(assignTemp(outIt->second));
                        continue;
                    }
                    auto entryIt = entryParamDefs.find(reg);
                    if (entryIt != entryParamDefs.end())
                    {
                        inputs.push_back(assignTemp(entryIt->second));
                    }
                }
                Instruction phiInstruction{
                    Opcode::PHI, std::move(inputs), {assignTemp(defId)}, std::nullopt};
                newInstructions.push_back(std::move(phiInstruction));
                current[reg] = defId;
            }
        }

        for (size_t instIndex = 0; instIndex < block.instructions.size(); ++instIndex)
        {
            auto instruction = block.instructions[instIndex];
            for (auto& input : instruction.inputs)
            {
                if (input.kind == ValueKind::REGISTER)
                {
                    auto it = current.find(input.reg);
                    if (it == current.end())
                    {
                        auto entryIt = entryParamDefs.find(input.reg);
                        it = current.insert({input.reg, entryIt->second}).first;
                    }
                    input = assignTemp(it->second);
                }
            }
            for (size_t outputIndex = 0; outputIndex < instruction.outputs.size(); ++outputIndex)
            {
                auto& output = instruction.outputs[outputIndex];
                if (output.kind == ValueKind::REGISTER)
                {
                    Register reg = output.reg;
                    u32 defId = *defIds[blockIndex][instIndex][outputIndex];
                    output = assignTemp(defId);
                    current[reg] = defId;
                }
            }
            newInstructions.push_back(std::move(instruction));
        }

        block.instructions = std::move(newInstructions);
    }

    return result;
}

} // namespace ir
} // namespace psxrecomp
