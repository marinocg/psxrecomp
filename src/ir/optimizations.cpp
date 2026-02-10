#include "psxrecomp/ir/optimizations.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace ir
{

namespace
{
bool hasSideEffects(Opcode opcode)
{
    switch (opcode)
    {
    case Opcode::STORE:
    case Opcode::MMIO_STORE:
    case Opcode::CALL:
    case Opcode::SYSCALL:
    case Opcode::BRANCH:
    case Opcode::JUMP:
    case Opcode::RETURN:
        return true;
    default:
        return false;
    }
}

bool isPureBinaryOp(Opcode opcode)
{
    switch (opcode)
    {
    case Opcode::ADD:
    case Opcode::SUB:
    case Opcode::AND:
    case Opcode::OR:
    case Opcode::XOR:
    case Opcode::SHL:
    case Opcode::SHR_LOGICAL:
    case Opcode::SHR_ARITH:
    case Opcode::COMPARE_EQ:
    case Opcode::COMPARE_NE:
    case Opcode::COMPARE_LT:
    case Opcode::COMPARE_LTU:
    case Opcode::COMPARE_LE:
    case Opcode::COMPARE_GT:
    case Opcode::COMPARE_GE:
        return true;
    default:
        return false;
    }
}

bool isCommutative(Opcode opcode)
{
    switch (opcode)
    {
    case Opcode::ADD:
    case Opcode::AND:
    case Opcode::OR:
    case Opcode::XOR:
    case Opcode::MUL:
    case Opcode::MULU:
        return true;
    default:
        return false;
    }
}

std::vector<Value> normalizedInputs(Opcode opcode, const std::vector<Value>& inputs)
{
    if (!isCommutative(opcode) || inputs.size() != 2)
    {
        return inputs;
    }
    if (inputs[0].toString() <= inputs[1].toString())
    {
        return inputs;
    }
    return {inputs[1], inputs[0]};
}

struct ValueKey
{
    ValueKind kind = ValueKind::INVALID;
    Register reg = 0;
    s32 immediate = 0;
    Address address = 0;
    u32 temporaryId = 0;
    SpecialRegister special = SpecialRegister::HI;

    bool operator==(const ValueKey& other) const
    {
        return kind == other.kind && reg == other.reg && immediate == other.immediate &&
               address == other.address && temporaryId == other.temporaryId &&
               special == other.special;
    }
};

struct InstructionKey
{
    Opcode opcode = Opcode::NOP;
    std::vector<ValueKey> inputs;

    bool operator==(const InstructionKey& other) const
    {
        return opcode == other.opcode && inputs == other.inputs;
    }
};

struct InstructionKeyHash
{
    size_t operator()(const InstructionKey& key) const
    {
        size_t hash = static_cast<size_t>(key.opcode);
        for (const auto& input : key.inputs)
        {
            hash ^= static_cast<size_t>(input.kind) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= static_cast<size_t>(input.reg) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= static_cast<size_t>(input.immediate) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= static_cast<size_t>(input.address) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= static_cast<size_t>(input.temporaryId) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= static_cast<size_t>(input.special) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

ValueKey toKey(const Value& value)
{
    return {value.kind,    value.reg,         value.immediate,
            value.address, value.temporaryId, value.specialReg};
}

bool isInvariantInput(const Value& value)
{
    return value.kind == ValueKind::IMMEDIATE || value.kind == ValueKind::REGISTER ||
           value.kind == ValueKind::SPECIAL || value.kind == ValueKind::ADDRESS;
}

std::unordered_set<u32>
collectLiveOutTemporaries(const BasicBlock& block,
                          const std::unordered_map<std::string, const BasicBlock*>& byName)
{
    std::unordered_set<u32> liveOut;
    for (const auto& successorName : block.successors)
    {
        auto it = byName.find(successorName);
        if (it == byName.end() || it->second == nullptr)
        {
            continue;
        }
        for (const auto& instruction : it->second->instructions)
        {
            for (const auto& input : instruction.inputs)
            {
                if (input.kind == ValueKind::TEMPORARY)
                {
                    liveOut.insert(input.temporaryId);
                }
            }
        }
    }
    return liveOut;
}

} // namespace

OptimizationStats runOptimizations(Function& function)
{
    OptimizationStats stats;

    for (auto& block : function.blocks)
    {
        for (auto& instruction : block.instructions)
        {
            if (!isPureBinaryOp(instruction.opcode) || instruction.inputs.size() < 2 ||
                instruction.outputs.empty())
            {
                continue;
            }
            const auto& lhs = instruction.inputs[0];
            const auto& rhs = instruction.inputs[1];
            if (lhs.kind != ValueKind::IMMEDIATE || rhs.kind != ValueKind::IMMEDIATE)
            {
                continue;
            }
            s32 result = 0;
            switch (instruction.opcode)
            {
            case Opcode::ADD:
                result = lhs.immediate + rhs.immediate;
                break;
            case Opcode::SUB:
                result = lhs.immediate - rhs.immediate;
                break;
            case Opcode::AND:
                result = lhs.immediate & rhs.immediate;
                break;
            case Opcode::OR:
                result = lhs.immediate | rhs.immediate;
                break;
            case Opcode::XOR:
                result = lhs.immediate ^ rhs.immediate;
                break;
            case Opcode::SHL:
                result =
                    static_cast<s32>(static_cast<u32>(lhs.immediate) << (rhs.immediate & 0x1F));
                break;
            case Opcode::SHR_LOGICAL:
                result =
                    static_cast<s32>(static_cast<u32>(lhs.immediate) >> (rhs.immediate & 0x1F));
                break;
            case Opcode::SHR_ARITH:
                result = lhs.immediate >> (rhs.immediate & 0x1F);
                break;
            case Opcode::COMPARE_EQ:
                result = (lhs.immediate == rhs.immediate) ? 1 : 0;
                break;
            case Opcode::COMPARE_NE:
                result = (lhs.immediate != rhs.immediate) ? 1 : 0;
                break;
            case Opcode::COMPARE_LT:
                result = (lhs.immediate < rhs.immediate) ? 1 : 0;
                break;
            case Opcode::COMPARE_LTU:
                result =
                    (static_cast<u32>(lhs.immediate) < static_cast<u32>(rhs.immediate)) ? 1 : 0;
                break;
            case Opcode::COMPARE_LE:
                result = (lhs.immediate <= rhs.immediate) ? 1 : 0;
                break;
            case Opcode::COMPARE_GT:
                result = (lhs.immediate > rhs.immediate) ? 1 : 0;
                break;
            case Opcode::COMPARE_GE:
                result = (lhs.immediate >= rhs.immediate) ? 1 : 0;
                break;
            default:
                continue;
            }
            instruction.opcode = Opcode::MOVE;
            instruction.inputs = {Value::makeImmediate(result)};
            stats.constantsFolded += 1;
        }
    }

    for (auto& block : function.blocks)
    {
        std::unordered_map<InstructionKey, u32, InstructionKeyHash> cseMap;
        for (auto& instruction : block.instructions)
        {
            if (!isPureBinaryOp(instruction.opcode) || instruction.outputs.empty() ||
                instruction.outputs.front().kind != ValueKind::TEMPORARY)
            {
                continue;
            }
            InstructionKey key;
            key.opcode = instruction.opcode;
            auto normalized = normalizedInputs(instruction.opcode, instruction.inputs);
            for (const auto& input : normalized)
            {
                key.inputs.push_back(toKey(input));
            }

            auto it = cseMap.find(key);
            if (it != cseMap.end())
            {
                instruction.opcode = Opcode::MOVE;
                instruction.inputs = {Value::makeTemporary(it->second)};
                stats.cseReplacements += 1;
                continue;
            }
            cseMap.emplace(std::move(key), instruction.outputs.front().temporaryId);
        }
    }

    for (auto& block : function.blocks)
    {
        bool hasSelfLoop = std::find(block.successors.begin(), block.successors.end(),
                                     block.name) != block.successors.end();
        if (!hasSelfLoop)
        {
            continue;
        }

        size_t insertIndex = 0;
        while (insertIndex < block.instructions.size() &&
               block.instructions[insertIndex].opcode == Opcode::PHI)
        {
            ++insertIndex;
        }

        std::vector<size_t> hoistedIndices;
        for (size_t index = insertIndex; index < block.instructions.size(); ++index)
        {
            const auto& instruction = block.instructions[index];
            if (hasSideEffects(instruction.opcode) || instruction.outputs.empty() ||
                instruction.outputs.front().kind != ValueKind::TEMPORARY)
            {
                continue;
            }
            bool invariant = true;
            for (const auto& input : instruction.inputs)
            {
                if (!isInvariantInput(input))
                {
                    invariant = false;
                    break;
                }
            }
            if (invariant)
            {
                hoistedIndices.push_back(index);
                stats.licmMoved += 1;
            }
        }

        if (!hoistedIndices.empty())
        {
            std::vector<Instruction> reordered;
            reordered.reserve(block.instructions.size());
            for (size_t index = 0; index < insertIndex; ++index)
            {
                reordered.push_back(block.instructions[index]);
            }
            for (size_t index : hoistedIndices)
            {
                reordered.push_back(block.instructions[index]);
            }
            for (size_t index = insertIndex; index < block.instructions.size(); ++index)
            {
                if (std::find(hoistedIndices.begin(), hoistedIndices.end(), index) ==
                    hoistedIndices.end())
                {
                    reordered.push_back(block.instructions[index]);
                }
            }
            block.instructions = std::move(reordered);
        }
    }

    std::unordered_map<std::string, const BasicBlock*> blockByName;
    for (const auto& block : function.blocks)
    {
        blockByName.emplace(block.name, &block);
    }

    for (auto& block : function.blocks)
    {
        std::unordered_set<u32> liveTemps = collectLiveOutTemporaries(block, blockByName);
        for (auto it = block.instructions.rbegin(); it != block.instructions.rend();)
        {
            const Instruction& instruction = *it;
            bool outputsOnlyTemps = !instruction.outputs.empty();
            bool outputsLive = false;
            for (const auto& output : instruction.outputs)
            {
                if (output.kind != ValueKind::TEMPORARY)
                {
                    outputsOnlyTemps = false;
                    break;
                }
                if (liveTemps.count(output.temporaryId) > 0)
                {
                    outputsLive = true;
                }
            }

            if (!hasSideEffects(instruction.opcode) && outputsOnlyTemps && !outputsLive)
            {
                it = decltype(it){block.instructions.erase(std::next(it).base())};
                stats.deadInstructionsRemoved += 1;
                continue;
            }

            for (const auto& input : instruction.inputs)
            {
                if (input.kind == ValueKind::TEMPORARY)
                {
                    liveTemps.insert(input.temporaryId);
                }
            }
            for (const auto& output : instruction.outputs)
            {
                if (output.kind == ValueKind::TEMPORARY)
                {
                    liveTemps.erase(output.temporaryId);
                }
            }
            ++it;
        }
    }

    return stats;
}

OptimizationStats runOptimizations(Program& program)
{
    OptimizationStats total;
    for (auto& function : program.functions)
    {
        OptimizationStats stats = runOptimizations(function);
        total.constantsFolded += stats.constantsFolded;
        total.deadInstructionsRemoved += stats.deadInstructionsRemoved;
        total.cseReplacements += stats.cseReplacements;
        total.licmMoved += stats.licmMoved;
    }
    return total;
}

} // namespace ir
} // namespace psxrecomp
