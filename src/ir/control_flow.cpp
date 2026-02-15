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
    constexpr const char* ExternalBlockName = "block_external";

    if (instructions.empty())
    {
        result.errors.push_back("No instructions provided for control-flow construction.");
        return result;
    }

    std::vector<Address> orderedAddresses;
    orderedAddresses.reserve(instructions.size());
    for (const auto& instruction : instructions)
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

    if (std::find(orderedAddresses.begin(), orderedAddresses.end(), entryAddress) ==
        orderedAddresses.end())
    {
        result.errors.push_back("Entry address not found in instruction stream.");
        return result;
    }

    std::unordered_set<Address> blockStarts;
    blockStarts.insert(entryAddress);

    size_t addressableIndex = 0;
    for (const auto& instruction : instructions)
    {
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
            else if (instruction.opcode == Opcode::BRANCH)
            {
                result.errors.push_back("Control-flow instruction missing target address.");
            }
        }

        if (isTerminator(instruction.opcode))
        {
            if (addressableIndex + 1 < orderedAddresses.size())
            {
                blockStarts.insert(orderedAddresses[addressableIndex + 1]);
            }
        }
        ++addressableIndex;
    }

    BasicBlock* currentBlock = nullptr;
    for (size_t index = 0; index < instructions.size(); ++index)
    {
        const auto& instruction = instructions[index];
        if (!instruction.sourceAddress.has_value())
        {
            continue;
        }
        Address address = *instruction.sourceAddress;
        if (blockStarts.count(address) > 0)
        {
            result.function.blocks.push_back(BasicBlock{formatBlockName(address), {}, {}, {}});
            currentBlock = &result.function.blocks.back();
            result.addressToBlockName[address] = currentBlock->name;
        }

        if (currentBlock == nullptr)
        {
            result.function.blocks.push_back(BasicBlock{"block_orphan", {}, {}, {}});
            currentBlock = &result.function.blocks.back();
        }

        currentBlock->instructions.push_back(instruction);
    }

    std::unordered_map<Address, Address> nextAddressByAddress;
    for (size_t index = 0; index + 1 < orderedAddresses.size(); ++index)
    {
        nextAddressByAddress[orderedAddresses[index]] = orderedAddresses[index + 1];
    }

    bool needsExternalBlock = false;
    // Track continuations: predecessor block name → continuation block name.
    // When a block jumps to an external address (outside this function),
    // the continuation is the block at the next sequential address.
    std::unordered_map<std::string, std::string> externalContinuations;

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
                needsExternalBlock = true;
                if (std::find(block.successors.begin(), block.successors.end(),
                              ExternalBlockName) == block.successors.end())
                {
                    block.successors.push_back(ExternalBlockName);
                }
                // Record the continuation for this block: resume at the next
                // sequential address after the external jump/call.
                if (nextAddress.has_value())
                {
                    auto continuationIt = result.addressToBlockName.find(*nextAddress);
                    if (continuationIt != result.addressToBlockName.end())
                    {
                        externalContinuations[block.name] = continuationIt->second;
                    }
                }
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
                needsExternalBlock = true;
                if (std::find(block.successors.begin(), block.successors.end(),
                              ExternalBlockName) == block.successors.end())
                {
                    block.successors.push_back(ExternalBlockName);
                }
                // For indirect jumps, record continuation at next sequential address.
                if (nextAddress.has_value())
                {
                    auto continuationIt = result.addressToBlockName.find(*nextAddress);
                    if (continuationIt != result.addressToBlockName.end())
                    {
                        externalContinuations[block.name] = continuationIt->second;
                    }
                }
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

    if (needsExternalBlock)
    {
        auto hasExternalBlock =
            std::any_of(result.function.blocks.begin(), result.function.blocks.end(),
                        [](const BasicBlock& block) { return block.name == ExternalBlockName; });
        if (!hasExternalBlock)
        {
            BasicBlock externalBlock{ExternalBlockName, {}, {}, {}};
            externalBlock.continuations = std::move(externalContinuations);
            result.function.blocks.push_back(std::move(externalBlock));
        }
        else
        {
            // Merge continuations into existing external block.
            for (auto& block : result.function.blocks)
            {
                if (block.name == ExternalBlockName)
                {
                    for (auto& entry : externalContinuations)
                    {
                        block.continuations[entry.first] = entry.second;
                    }
                    break;
                }
            }
        }
    }

    return result;
}

} // namespace ir
} // namespace psxrecomp
