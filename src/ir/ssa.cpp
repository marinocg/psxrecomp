#include "psxrecomp/ir/ssa.h"

#include "psxrecomp/types.h"

#include <algorithm>
#include <array>
#include <optional>
#include <unordered_map>

namespace psxrecomp
{
namespace ir
{
namespace
{
constexpr size_t kRegisterCount = Registers::NUM_REGISTERS;

u32 nextTemporaryId(const Function& function)
{
    u32 maxId = 0;
    bool hasTemp = false;
    for (const auto& block : function.blocks)
    {
        for (const auto& instruction : block.instructions)
        {
            for (const auto& value : instruction.inputs)
            {
                if (value.kind == ValueKind::TEMPORARY)
                {
                    maxId = std::max(maxId, value.temporaryId);
                    hasTemp = true;
                }
            }
            for (const auto& value : instruction.outputs)
            {
                if (value.kind == ValueKind::TEMPORARY)
                {
                    maxId = std::max(maxId, value.temporaryId);
                    hasTemp = true;
                }
            }
        }
    }
    return hasTemp ? maxId + 1 : 0;
}

struct BlockState
{
    std::array<Value, kRegisterCount> in;
    std::array<Value, kRegisterCount> out;
    std::array<std::optional<Value>, kRegisterCount> phiOutputs;
};

struct InstructionOutputInfo
{
    std::vector<std::optional<Register>> outputRegisters;
};

std::array<Value, kRegisterCount> makeRegisterDefaults()
{
    std::array<Value, kRegisterCount> values{};
    for (size_t index = 0; index < kRegisterCount; ++index)
    {
        values[index] = Value::makeRegister(static_cast<Register>(index));
    }
    return values;
}
} // namespace

SsaResult convertFunctionToSSA(Function& function, const ControlFlowGraph& graph)
{
    SsaResult result;
    if (function.blocks.empty())
    {
        return result;
    }

    u32 tempId = nextTemporaryId(function);
    std::vector<std::vector<InstructionOutputInfo>> outputInfo(function.blocks.size());

    for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
    {
        auto& block = function.blocks[blockIndex];
        auto& blockInfo = outputInfo[blockIndex];
        blockInfo.reserve(block.instructions.size());
        for (auto& instruction : block.instructions)
        {
            InstructionOutputInfo info;
            info.outputRegisters.reserve(instruction.outputs.size());
            for (auto& output : instruction.outputs)
            {
                if (output.kind == ValueKind::REGISTER)
                {
                    info.outputRegisters.emplace_back(output.reg);
                    output = Value::makeTemporary(tempId++);
                    result.changed = true;
                }
                else
                {
                    info.outputRegisters.emplace_back(std::nullopt);
                }
            }
            blockInfo.push_back(std::move(info));
        }
    }

    std::vector<BlockState> blockStates(function.blocks.size());
    auto registerDefaults = makeRegisterDefaults();

    for (auto& state : blockStates)
    {
        state.in = registerDefaults;
        state.out = registerDefaults;
    }

    std::vector<size_t> rpo = computeReversePostOrder(graph);
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t blockIndex : rpo)
        {
            auto& block = function.blocks[blockIndex];
            auto& state = blockStates[blockIndex];

            std::array<Value, kRegisterCount> newIn = registerDefaults;
            if (!graph.predecessors[blockIndex].empty())
            {
                for (size_t reg = 0; reg < kRegisterCount; ++reg)
                {
                    const auto& preds = graph.predecessors[blockIndex];
                    Value merged = blockStates[preds[0]].out[reg];
                    bool needsPhi = false;
                    for (size_t predIndex = 1; predIndex < preds.size(); ++predIndex)
                    {
                        const auto& candidate = blockStates[preds[predIndex]].out[reg];
                        if (!(candidate == merged))
                        {
                            needsPhi = true;
                            break;
                        }
                    }

                    if (needsPhi)
                    {
                        if (!state.phiOutputs[reg].has_value())
                        {
                            state.phiOutputs[reg] = Value::makeTemporary(tempId++);
                            result.changed = true;
                            changed = true;
                        }
                        newIn[reg] = *state.phiOutputs[reg];
                    }
                    else
                    {
                        newIn[reg] = merged;
                    }
                }
            }

            if (newIn != state.in)
            {
                state.in = newIn;
                changed = true;
            }

            auto newOut = state.in;
            for (size_t instructionIndex = 0; instructionIndex < block.instructions.size();
                 ++instructionIndex)
            {
                const auto& instruction = block.instructions[instructionIndex];
                const auto& info = outputInfo[blockIndex][instructionIndex].outputRegisters;
                for (size_t outputIndex = 0; outputIndex < info.size(); ++outputIndex)
                {
                    if (info[outputIndex].has_value())
                    {
                        Register reg = *info[outputIndex];
                        newOut[reg] = instruction.outputs[outputIndex];
                    }
                }
            }

            if (newOut != state.out)
            {
                state.out = newOut;
                changed = true;
            }
        }
    }

    for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
    {
        auto& block = function.blocks[blockIndex];
        auto& state = blockStates[blockIndex];
        if (graph.predecessors[blockIndex].empty())
        {
            continue;
        }

        std::vector<Instruction> phiInstructions;
        std::vector<InstructionOutputInfo> phiInfos;
        for (size_t reg = 0; reg < kRegisterCount; ++reg)
        {
            if (!state.phiOutputs[reg].has_value())
            {
                continue;
            }
            std::vector<Value> inputs;
            inputs.reserve(graph.predecessors[blockIndex].size());
            for (size_t predIndex : graph.predecessors[blockIndex])
            {
                inputs.push_back(blockStates[predIndex].out[reg]);
            }
            phiInstructions.push_back(Instruction{
                Opcode::PHI, std::move(inputs), {*state.phiOutputs[reg]}, std::nullopt});
            InstructionOutputInfo phiInfo;
            phiInfo.outputRegisters.resize(1);
            phiInfos.push_back(std::move(phiInfo));
        }

        if (!phiInstructions.empty())
        {
            block.instructions.insert(block.instructions.begin(), phiInstructions.begin(),
                                      phiInstructions.end());
            auto& blockInfo = outputInfo[blockIndex];
            blockInfo.insert(blockInfo.begin(), phiInfos.begin(), phiInfos.end());
            result.changed = true;
        }
    }

    for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex)
    {
        auto& block = function.blocks[blockIndex];
        auto currentValues = blockStates[blockIndex].in;

        for (size_t instructionIndex = 0; instructionIndex < block.instructions.size();
             ++instructionIndex)
        {
            auto& instruction = block.instructions[instructionIndex];
            if (instruction.opcode != Opcode::PHI)
            {
                for (auto& input : instruction.inputs)
                {
                    if (input.kind == ValueKind::REGISTER)
                    {
                        input = currentValues[input.reg];
                    }
                }
            }

            const auto& info = outputInfo[blockIndex][instructionIndex].outputRegisters;
            for (size_t outputIndex = 0; outputIndex < info.size(); ++outputIndex)
            {
                if (info[outputIndex].has_value())
                {
                    Register reg = *info[outputIndex];
                    currentValues[reg] = instruction.outputs[outputIndex];
                }
            }
        }
    }

    return result;
}

} // namespace ir
} // namespace psxrecomp
