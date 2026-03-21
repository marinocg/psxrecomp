#include "pipeline_harvest_internal.h"

#include "pipeline_memory_reference_helpers.h"

#include <algorithm>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{

std::optional<Address>
findReturnedCodePointer(const std::vector<disasm::Instruction>& disassembled,
                        const InstructionIndexMap& instructionIndexMap,
                        const std::vector<disasm::FunctionBoundary>& knownBoundaries,
                        Address functionStart, Address baseAddress, Address endAddress)
{
    const auto boundaryIt = std::find_if(knownBoundaries.begin(), knownBoundaries.end(),
                                         [functionStart](const disasm::FunctionBoundary& boundary)
                                         { return boundary.start == functionStart; });
    if (boundaryIt == knownBoundaries.end())
    {
        return std::nullopt;
    }

    auto rangeBegin = std::lower_bound(disassembled.begin(), disassembled.end(), boundaryIt->start,
                                       [](const disasm::Instruction& instruction, Address target)
                                       { return instruction.address < target; });
    auto rangeEnd = std::upper_bound(rangeBegin, disassembled.end(), boundaryIt->end,
                                     [](Address target, const disasm::Instruction& instruction)
                                     { return target < instruction.address; });
    std::vector<disasm::Instruction> functionInstructions(rangeBegin, rangeEnd);
    for (size_t index = 0; index < functionInstructions.size(); ++index)
    {
        const auto& instruction = functionInstructions[index];
        if (instruction.opcode != disasm::Opcode::LUI || instruction.rt != Registers::V0)
        {
            continue;
        }

        std::optional<Address> builtAddress;
        size_t builtAddressIndex = 0;
        const u32 hiImm = instruction.immediate & 0xFFFFu;
        for (size_t lookAhead = 1;
             lookAhead <= 6 && index + lookAhead < functionInstructions.size(); ++lookAhead)
        {
            const auto& next = functionInstructions[index + lookAhead];
            if (next.opcode == disasm::Opcode::ADDIU && next.rs == Registers::V0 &&
                next.rt == Registers::V0)
            {
                const u32 lo = next.immediate & 0xFFFFu;
                const s32 signedLo =
                    (lo & 0x8000u) ? static_cast<s32>(lo | 0xFFFF0000u) : static_cast<s32>(lo);
                builtAddress = static_cast<Address>((hiImm << 16) + static_cast<u32>(signedLo));
                builtAddressIndex = index + lookAhead;
                break;
            }
            if (next.opcode == disasm::Opcode::ORI && next.rs == Registers::V0 &&
                next.rt == Registers::V0)
            {
                builtAddress = static_cast<Address>((hiImm << 16) | (next.immediate & 0xFFFFu));
                builtAddressIndex = index + lookAhead;
                break;
            }
            if (writesRegister(next, Registers::V0))
            {
                break;
            }
        }
        if (!builtAddress.has_value())
        {
            continue;
        }

        bool returnsBuiltPointer = false;
        for (size_t lookAhead = 1;
             lookAhead <= 6 && builtAddressIndex + lookAhead < functionInstructions.size();
             ++lookAhead)
        {
            const auto& next = functionInstructions[builtAddressIndex + lookAhead];
            if (next.opcode == disasm::Opcode::JR && next.rs == Registers::RA)
            {
                returnsBuiltPointer = true;
                break;
            }
            if (writesRegister(next, Registers::V0))
            {
                break;
            }
        }
        if (!returnsBuiltPointer)
        {
            continue;
        }

        const Address target = *builtAddress;
        if (target < baseAddress || target >= endAddress || (target % 4) != 0 ||
            !isDecodableInstruction(disassembled, instructionIndexMap, target))
        {
            continue;
        }
        if (looksLikeFunctionEntry(disassembled, instructionIndexMap, target) ||
            looksLikeIndirectTargetEntry(disassembled, instructionIndexMap, target) ||
            looksLikeCallableCodeRegion(disassembled, instructionIndexMap, target) ||
            looksLikeGapAdjacentCallableEntry(disassembled, instructionIndexMap, knownBoundaries,
                                              target))
        {
            return target;
        }
    }

    return std::nullopt;
}

} // namespace

std::vector<Address>
harvestReturnedCodePointerSeeds(const std::vector<disasm::Instruction>& disassembled,
                                const InstructionIndexMap& instructionIndexMap,
                                const std::vector<disasm::FunctionBoundary>& knownBoundaries,
                                Address baseAddress, Address endAddress)
{
    std::vector<Address> results;
    for (size_t index = 0; index < disassembled.size(); ++index)
    {
        const auto& instruction = disassembled[index];
        if (instruction.opcode != disasm::Opcode::JAL)
        {
            continue;
        }
        const auto jumpTarget = instruction.getJumpTarget();
        if (!jumpTarget.has_value())
        {
            continue;
        }

        const auto returnedCodePointer =
            findReturnedCodePointer(disassembled, instructionIndexMap, knownBoundaries, *jumpTarget,
                                    baseAddress, endAddress);
        if (!returnedCodePointer.has_value())
        {
            continue;
        }

        for (size_t lookAhead = 2; lookAhead <= 12 && index + lookAhead < disassembled.size();
             ++lookAhead)
        {
            const auto& next = disassembled[index + lookAhead];
            if (isWordMemoryReferenceOpcode(next.opcode) && next.rt == Registers::V0 &&
                next.rs != Registers::SP)
            {
                appendUniqueSorted(results, *returnedCodePointer);
                break;
            }
            if (writesRegister(next, Registers::V0))
            {
                break;
            }
        }
    }
    return results;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
