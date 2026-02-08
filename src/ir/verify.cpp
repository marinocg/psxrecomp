#include "psxrecomp/ir/verify.h"

#include <array>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace ir
{
namespace
{
struct OperandSpec
{
    size_t minInputs;
    size_t maxInputs;
    size_t minOutputs;
    size_t maxOutputs;
};

OperandSpec operandSpecFor(Opcode opcode)
{
    switch (opcode)
    {
    case Opcode::NOP:
        return {0, 0, 0, 0};
    case Opcode::MOVE:
        return {1, 1, 1, 1};
    case Opcode::ADD:
    case Opcode::SUB:
    case Opcode::AND:
    case Opcode::OR:
    case Opcode::XOR:
        return {2, 2, 1, 1};
    case Opcode::LOAD:
        return {1, 1, 1, 1};
    case Opcode::STORE:
        return {2, 2, 0, 0};
    case Opcode::BRANCH:
        return {1, 2, 0, 0};
    case Opcode::JUMP:
        return {1, 1, 0, 0};
    case Opcode::CALL:
        return {0, static_cast<size_t>(-1), 0, 1};
    case Opcode::RETURN:
        return {0, 1, 0, 0};
    case Opcode::PHI:
        return {1, static_cast<size_t>(-1), 1, 1};
    }
    return {0, 0, 0, 0};
}

bool countInRange(size_t value, size_t min, size_t max)
{
    return value >= min && value <= max;
}

void addError(VerificationResult& result, const std::string& message)
{
    result.ok = false;
    result.errors.push_back(message);
}
} // namespace

VerificationResult verifyFunction(const Function& function, const ControlFlowGraph& graph)
{
    VerificationResult result;

    for (const auto& block : function.blocks)
    {
        if (block.name.empty())
        {
            addError(result, "Block has empty name.");
        }
    }

    for (size_t index = 0; index < graph.blocks.size(); ++index)
    {
        if (!graph.blocks[index])
        {
            addError(result, "CFG contains null block pointer at index " + std::to_string(index));
        }
    }

    std::unordered_map<u32, std::string> tempDefinitions;
    std::unordered_set<u32> tempUses;

    for (const auto& block : function.blocks)
    {
        for (const auto& instruction : block.instructions)
        {
            const OperandSpec spec = operandSpecFor(instruction.opcode);
            if (!countInRange(instruction.inputs.size(), spec.minInputs, spec.maxInputs) ||
                !countInRange(instruction.outputs.size(), spec.minOutputs, spec.maxOutputs))
            {
                std::ostringstream message;
                message << "Invalid operand count for opcode " << instruction.toString()
                        << " in block '" << block.name << "'";
                addError(result, message.str());
            }

            for (const auto& input : instruction.inputs)
            {
                if (input.kind == ValueKind::TEMPORARY)
                {
                    tempUses.insert(input.temporaryId);
                }
            }

            for (const auto& output : instruction.outputs)
            {
                if (output.kind == ValueKind::TEMPORARY)
                {
                    auto [it, inserted] = tempDefinitions.emplace(
                        output.temporaryId, "Block '" + block.name + "'");
                    if (!inserted)
                    {
                        addError(result, "Temporary t" + std::to_string(output.temporaryId) +
                                              " defined multiple times (first in " + it->second +
                                              ")");
                    }
                }
            }

            if (instruction.opcode == Opcode::PHI)
            {
                auto blockIndex = graph.indexOf(block.name);
                if (blockIndex.has_value())
                {
                    const auto& preds = graph.predecessors[*blockIndex];
                    if (!preds.empty() && instruction.inputs.size() != preds.size())
                    {
                        addError(result, "Phi in block '" + block.name +
                                              "' has input count mismatch with predecessors");
                    }
                }
            }
        }
    }

    for (u32 useId : tempUses)
    {
        if (tempDefinitions.find(useId) == tempDefinitions.end())
        {
            addError(result, "Temporary t" + std::to_string(useId) + " used before definition");
        }
    }

    return result;
}

VerificationResult verifyProgram(const Program& program)
{
    VerificationResult result;
    for (const auto& function : program.functions)
    {
        std::vector<std::string> cfgErrors;
        auto graph = buildControlFlowGraph(function, &cfgErrors);
        for (const auto& error : cfgErrors)
        {
            addError(result, "CFG error in function '" + function.name + "': " + error);
        }
        auto functionResult = verifyFunction(function, graph);
        if (!functionResult.ok)
        {
            result.ok = false;
            result.errors.insert(result.errors.end(), functionResult.errors.begin(),
                                 functionResult.errors.end());
        }
    }
    return result;
}

} // namespace ir
} // namespace psxrecomp
