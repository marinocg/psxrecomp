#include "analysis_helpers.h"

namespace psxrecomp
{
namespace disasm
{
namespace detail
{

namespace
{
bool isStackAdjust(const Instruction& instruction, bool negative)
{
    if (instruction.opcode != Opcode::ADDIU)
    {
        return false;
    }

    if (instruction.rs != kStackPointer || instruction.rt != kStackPointer)
    {
        return false;
    }

    return negative ? instruction.immediate < 0 : instruction.immediate > 0;
}

bool isRegisterSave(const Instruction& instruction, Register reg)
{
    return instruction.opcode == Opcode::SW && instruction.rs == kStackPointer &&
           instruction.rt == reg;
}
} // namespace

bool hasProloguePattern(const std::vector<Instruction>& instructions, size_t index)
{
    if (index >= instructions.size())
    {
        return false;
    }

    if (!isStackAdjust(instructions[index], true))
    {
        return false;
    }

    for (size_t offset = 1; offset <= 2 && index + offset < instructions.size(); ++offset)
    {
        if (isRegisterSave(instructions[index + offset], kReturnAddress))
        {
            return true;
        }
    }

    return false;
}

bool hasEpiloguePattern(const std::vector<Instruction>& instructions, size_t index)
{
    if (index >= instructions.size())
    {
        return false;
    }

    if (!instructions[index].isReturn())
    {
        return false;
    }

    if (index == 0)
    {
        return true;
    }

    if (index >= 1 && isStackAdjust(instructions[index - 1], false))
    {
        return true;
    }

    return true;
}

std::unordered_map<Address, size_t>
buildInstructionIndex(const std::vector<Instruction>& instructions)
{
    std::unordered_map<Address, size_t> indexMap;
    indexMap.reserve(instructions.size());
    for (size_t i = 0; i < instructions.size(); ++i)
    {
        indexMap[instructions[i].address] = i;
    }
    return indexMap;
}

std::optional<Address> resolveImmediateAddress(const std::vector<Instruction>& instructions,
                                               size_t index, Register reg)
{
    std::optional<u32> high;
    std::optional<s32> low;

    for (size_t step = 0; step < 6 && index >= step; ++step)
    {
        const Instruction& instruction = instructions[index - step];
        switch (instruction.opcode)
        {
        case Opcode::LUI:
            if (instruction.rt == reg)
            {
                high = static_cast<u32>(static_cast<u16>(instruction.immediate)) << 16;
            }
            break;
        case Opcode::ORI:
            if (instruction.rt == reg && instruction.rs == reg)
            {
                low = static_cast<u16>(instruction.immediate);
            }
            break;
        case Opcode::ADDIU:
            if (instruction.rt == reg && instruction.rs == reg)
            {
                low = instruction.immediate;
            }
            break;
        default:
            break;
        }

        if (high && low)
        {
            return static_cast<Address>(*high + static_cast<u32>(*low));
        }
    }

    if (high)
    {
        return static_cast<Address>(*high);
    }

    return std::nullopt;
}

std::vector<AddressRange> buildRanges(const std::vector<Instruction>& instructions,
                                      const std::unordered_set<Address>& addresses)
{
    std::vector<AddressRange> ranges;
    if (instructions.empty())
    {
        return ranges;
    }

    bool inRange = false;
    Address rangeStart = 0;
    Address previous = 0;

    for (const auto& instruction : instructions)
    {
        const bool isInSet = addresses.find(instruction.address) != addresses.end();
        if (isInSet && !inRange)
        {
            rangeStart = instruction.address;
            previous = instruction.address;
            inRange = true;
            continue;
        }

        if (isInSet && inRange && instruction.address == previous + 4)
        {
            previous = instruction.address;
            continue;
        }

        if (inRange && (!isInSet || instruction.address != previous + 4))
        {
            ranges.push_back({rangeStart, previous});
            inRange = false;
        }

        if (isInSet && !inRange)
        {
            rangeStart = instruction.address;
            previous = instruction.address;
            inRange = true;
        }
    }

    if (inRange)
    {
        ranges.push_back({rangeStart, previous});
    }

    return ranges;
}

bool isDirectCall(const Instruction& instruction)
{
    if (instruction.opcode == Opcode::JAL)
    {
        return true;
    }

    return instruction.opcode == Opcode::BLTZAL || instruction.opcode == Opcode::BGEZAL;
}

std::optional<Address> resolveDirectCallTarget(const Instruction& instruction)
{
    if (instruction.opcode == Opcode::JAL)
    {
        return instruction.getJumpTarget();
    }

    if (instruction.opcode == Opcode::BLTZAL || instruction.opcode == Opcode::BGEZAL)
    {
        return instruction.getBranchTarget();
    }

    return std::nullopt;
}

} // namespace detail
} // namespace disasm
} // namespace psxrecomp
