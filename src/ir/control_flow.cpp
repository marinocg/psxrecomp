#include "psxrecomp/ir/control_flow.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace psxrecomp
{
namespace ir
{

namespace
{
std::string formatBlockName(Address address)
{
    std::ostringstream stream;
    stream << "block_0x" << std::hex << address;
    return stream.str();
}

std::string formatAddress(Address address)
{
    std::ostringstream stream;
    stream << std::hex << address;
    return stream.str();
}

bool isTerminator(Opcode opcode)
{
    return opcode == Opcode::BRANCH || opcode == Opcode::JUMP || opcode == Opcode::RETURN;
}

std::optional<Address> extractTargetAddress(const Instruction& instruction)
{
    for (const auto& input : instruction.inputs)
    {
        if (input.kind == ValueKind::ADDRESS)
        {
            return input.address;
        }
    }
    return std::nullopt;
}
} // namespace

ControlFlowBuildResult buildControlFlowFunction(std::string_view functionName, Address entryAddress,
                                                const std::vector<Instruction>& instructions)
{
    ControlFlowBuildResult result{Function{std::string(functionName), entryAddress, {}}, {}, {}};

    if (instructions.empty())
    {
        result.errors.push_back("No instructions provided for control-flow construction.");
        return result;
    }

    std::vector<Instruction> sorted = instructions;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Instruction& lhs, const Instruction& rhs)
                     { return lhs.sourceAddress.value_or(0) < rhs.sourceAddress.value_or(0); });

    std::vector<Address> orderedAddresses;
    orderedAddresses.reserve(sorted.size());
    for (const auto& instruction : sorted)
    {
        if (!instruction.sourceAddress.has_value())
        {
            result.errors.push_back(
                "Instruction missing source address during control-flow build.");
            continue;
        }
        orderedAddresses.push_back(*instruction.sourceAddress);
    }

    if (orderedAddresses.empty())
    {
        result.errors.push_back("No addressable instructions available for control-flow build.");
        return result;
    }

    std::unordered_set<Address> blockStarts;
    blockStarts.insert(entryAddress);

    for (size_t index = 0; index < sorted.size(); ++index)
    {
        const auto& instruction = sorted[index];
        if (!instruction.sourceAddress.has_value())
        {
            continue;
        }
        if (instruction.opcode == Opcode::BRANCH || instruction.opcode == Opcode::JUMP)
        {
            auto target = extractTargetAddress(instruction);
            if (target.has_value())
            {
                blockStarts.insert(*target);
            }
            else
            {
                result.errors.push_back("Control-flow instruction missing target address.");
            }
        }

        if (isTerminator(instruction.opcode))
        {
            if (index + 1 < orderedAddresses.size())
            {
                blockStarts.insert(orderedAddresses[index + 1]);
            }
        }
    }

    BasicBlock* currentBlock = nullptr;
    for (size_t index = 0; index < sorted.size(); ++index)
    {
        const auto& instruction = sorted[index];
        if (!instruction.sourceAddress.has_value())
        {
            continue;
        }
        Address address = *instruction.sourceAddress;
        if (blockStarts.count(address) > 0)
        {
            result.function.blocks.push_back(BasicBlock{formatBlockName(address), {}, {}});
            currentBlock = &result.function.blocks.back();
            result.addressToBlockName[address] = currentBlock->name;
        }

        if (currentBlock == nullptr)
        {
            result.function.blocks.push_back(BasicBlock{"block_orphan", {}, {}});
            currentBlock = &result.function.blocks.back();
        }

        currentBlock->instructions.push_back(instruction);
    }

    std::unordered_map<Address, Address> nextAddressByAddress;
    for (size_t index = 0; index + 1 < orderedAddresses.size(); ++index)
    {
        nextAddressByAddress[orderedAddresses[index]] = orderedAddresses[index + 1];
    }

    for (auto& block : result.function.blocks)
    {
        if (block.instructions.empty())
        {
            continue;
        }
        const auto& lastInstruction = block.instructions.back();
        if (!lastInstruction.sourceAddress.has_value())
        {
            continue;
        }
        Address address = *lastInstruction.sourceAddress;
        auto nextAddressIt = nextAddressByAddress.find(address);
        std::optional<Address> nextAddress = std::nullopt;
        if (nextAddressIt != nextAddressByAddress.end())
        {
            nextAddress = nextAddressIt->second;
        }

        auto addSuccessor = [&](Address target)
        {
            auto successorIt = result.addressToBlockName.find(target);
            if (successorIt == result.addressToBlockName.end())
            {
                result.errors.push_back("Missing successor block for address 0x" +
                                        formatAddress(target));
                return;
            }
            if (std::find(block.successors.begin(), block.successors.end(), successorIt->second) ==
                block.successors.end())
            {
                block.successors.push_back(successorIt->second);
            }
        };

        switch (lastInstruction.opcode)
        {
        case Opcode::BRANCH:
        {
            auto target = extractTargetAddress(lastInstruction);
            if (target.has_value())
            {
                addSuccessor(*target);
            }
            else
            {
                result.errors.push_back("Branch missing target address.");
            }
            if (nextAddress.has_value())
            {
                addSuccessor(*nextAddress);
            }
            break;
        }
        case Opcode::JUMP:
        {
            auto target = extractTargetAddress(lastInstruction);
            if (target.has_value())
            {
                addSuccessor(*target);
            }
            else
            {
                result.errors.push_back("Jump missing target address.");
            }
            break;
        }
        case Opcode::RETURN:
            break;
        default:
            if (nextAddress.has_value())
            {
                addSuccessor(*nextAddress);
            }
            break;
        }
    }

    return result;
}

} // namespace ir
} // namespace psxrecomp
