#include "pipeline_memory_reference_helpers.h"

#include "pipeline_harvest_internal.h"

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{
namespace
{

std::optional<Register> copiedRegisterSource(const disasm::Instruction& instruction,
                                             Register destination)
{
    switch (instruction.opcode)
    {
    case disasm::Opcode::ADDU:
    case disasm::Opcode::OR:
        if (instruction.rd != destination)
        {
            return std::nullopt;
        }
        if (instruction.rs == Registers::ZERO && instruction.rt != Registers::ZERO)
        {
            return instruction.rt;
        }
        if (instruction.rt == Registers::ZERO && instruction.rs != Registers::ZERO)
        {
            return instruction.rs;
        }
        return std::nullopt;
    case disasm::Opcode::ADDIU:
    case disasm::Opcode::ORI:
        if (instruction.rt == destination && instruction.rs != Registers::ZERO &&
            instruction.immediate == 0)
        {
            return instruction.rs;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

std::optional<Address> acceptResolvedAddress(Address address, Address moduleBase, Address moduleEnd)
{
    if (moduleEnd == 0)
    {
        return std::optional<Address>{address};
    }
    if (address >= moduleBase && address + 3 < moduleEnd && (address % 4) == 0)
    {
        return std::optional<Address>{address};
    }
    return std::optional<Address>{};
}

std::optional<Address> resolveStaticMemoryReferenceAddressImpl(
    const std::vector<disasm::Instruction>& instructions, size_t memoryInstructionIndex,
    Register baseRegister, size_t searchEndIndex, Address moduleBase, Address moduleEnd)
{
    const auto& memoryInstruction = instructions[memoryInstructionIndex];
    Register trackedBase = baseRegister;
    std::optional<disasm::Instruction> lowHalfBuild;
    for (size_t lookBack = 1; lookBack <= 12 && lookBack <= searchEndIndex; ++lookBack)
    {
        const size_t candidateIndex = searchEndIndex - lookBack;
        const auto& candidate = instructions[candidateIndex];
        if (auto copiedSource = copiedRegisterSource(candidate, trackedBase);
            copiedSource.has_value())
        {
            trackedBase = *copiedSource;
            continue;
        }
        if (!lowHalfBuild.has_value() && candidate.rs == trackedBase &&
            candidate.rt == trackedBase &&
            (candidate.opcode == disasm::Opcode::ADDIU || candidate.opcode == disasm::Opcode::ORI))
        {
            lowHalfBuild = candidate;
            continue;
        }
        if (candidate.opcode != disasm::Opcode::LUI || candidate.rt != trackedBase)
        {
            if (writesRegister(candidate, trackedBase))
            {
                break;
            }
            continue;
        }
        const u32 hiImm = candidate.immediate & 0xFFFFu;
        Address baseAddress = static_cast<Address>(hiImm << 16);
        if (lowHalfBuild.has_value())
        {
            const u32 lo = lowHalfBuild->immediate & 0xFFFFu;
            baseAddress =
                lowHalfBuild->opcode == disasm::Opcode::ADDIU
                    ? static_cast<Address>((hiImm << 16) +
                                           static_cast<u32>((lo & 0x8000u) != 0
                                                                ? static_cast<s32>(lo | 0xFFFF0000u)
                                                                : static_cast<s32>(lo)))
                    : static_cast<Address>((hiImm << 16) | lo);
        }
        const Address resolvedAddress = static_cast<Address>(
            baseAddress + static_cast<u32>(static_cast<s32>(memoryInstruction.immediate)));
        if (auto result = acceptResolvedAddress(resolvedAddress, moduleBase, moduleEnd);
            result.has_value())
        {
            return result;
        }
    }
    return std::nullopt;
}

} // namespace

bool isWordMemoryReferenceOpcode(disasm::Opcode opcode)
{
    return opcode == disasm::Opcode::LW || opcode == disasm::Opcode::SW ||
           opcode == disasm::Opcode::LWL || opcode == disasm::Opcode::LWR ||
           opcode == disasm::Opcode::SWL || opcode == disasm::Opcode::SWR;
}

bool isKnownIndirectPointerWord(const std::unordered_set<Address>& knownWords, Address address)
{
    if (knownWords.find(address) != knownWords.end())
    {
        return true;
    }
    for (const Address knownWord : knownWords)
    {
        const Address distance = knownWord > address ? knownWord - address : address - knownWord;
        if (distance <= 64 && (distance % 4) == 0)
        {
            return true;
        }
    }
    return false;
}

std::optional<Address>
resolveStaticMemoryReferenceAddress(const std::vector<disasm::Instruction>& instructions,
                                    size_t memoryInstructionIndex, Address moduleBase,
                                    Address moduleEnd)
{
    const auto& memoryInstruction = instructions[memoryInstructionIndex];
    if (!isWordMemoryReferenceOpcode(memoryInstruction.opcode))
    {
        return std::nullopt;
    }
    return resolveStaticMemoryReferenceAddressImpl(instructions, memoryInstructionIndex,
                                                   memoryInstruction.rs, memoryInstructionIndex,
                                                   moduleBase, moduleEnd);
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
