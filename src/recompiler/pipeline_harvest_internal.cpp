#include "pipeline_harvest_internal.h"

#include "../disasm/analysis_helpers.h"

#include <algorithm>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

u32 readProgramWord(const std::vector<u8>& programData, Address baseAddress, Address address)
{
    const size_t offset = static_cast<size_t>(address - baseAddress);
    return static_cast<u32>(programData[offset]) |
           (static_cast<u32>(programData[offset + 1]) << 8) |
           (static_cast<u32>(programData[offset + 2]) << 16) |
           (static_cast<u32>(programData[offset + 3]) << 24);
}

bool isAddressInRanges(const std::vector<disasm::AddressRange>& ranges, Address address)
{
    return std::any_of(ranges.begin(), ranges.end(), [address](const disasm::AddressRange& range)
                       { return address >= range.start && address <= range.end; });
}

bool isDecodableInstruction(const std::vector<disasm::Instruction>& disassembled,
                            const InstructionIndexMap& instructionIndexMap, Address address)
{
    auto it = instructionIndexMap.find(address);
    return it != instructionIndexMap.end() &&
           disassembled[it->second].opcode != disasm::Opcode::UNKNOWN;
}

bool looksLikeFunctionEntry(const std::vector<disasm::Instruction>& disassembled,
                            const InstructionIndexMap& instructionIndexMap, Address address)
{
    auto it = instructionIndexMap.find(address);
    if (it == instructionIndexMap.end())
    {
        return false;
    }

    const size_t startIndex = it->second;
    if (disasm::detail::hasProloguePattern(disassembled, startIndex))
    {
        return true;
    }

    const auto hasNearbyStackPrologue = [&]()
    {
        const size_t prologueSearchLimit = std::min(startIndex + 4, disassembled.size());
        for (size_t prologueIndex = startIndex; prologueIndex < prologueSearchLimit;
             ++prologueIndex)
        {
            const auto& prologueInstruction = disassembled[prologueIndex];
            if (prologueInstruction.opcode != disasm::Opcode::ADDIU ||
                prologueInstruction.rs != Registers::SP ||
                prologueInstruction.rt != Registers::SP || prologueInstruction.immediate >= 0)
            {
                continue;
            }

            const size_t saveLimit = std::min(prologueIndex + 8, disassembled.size());
            for (size_t saveIndex = prologueIndex + 1; saveIndex < saveLimit; ++saveIndex)
            {
                const auto& saveInstruction = disassembled[saveIndex];
                if (saveInstruction.opcode == disasm::Opcode::SW &&
                    saveInstruction.rs == Registers::SP && saveInstruction.rt == Registers::RA)
                {
                    return true;
                }
            }
        }
        return false;
    };

    if (hasNearbyStackPrologue())
    {
        return true;
    }

    const auto& firstInstruction = disassembled[startIndex];
    if (firstInstruction.opcode == disasm::Opcode::J)
    {
        return true;
    }

    if (firstInstruction.opcode != disasm::Opcode::LUI &&
        firstInstruction.opcode != disasm::Opcode::ADDIU)
    {
        return false;
    }

    const size_t limitIndex = std::min(startIndex + 16, disassembled.size());
    for (size_t index = startIndex; index < limitIndex; ++index)
    {
        const auto& instruction = disassembled[index];
        if (instruction.opcode == disasm::Opcode::UNKNOWN)
        {
            break;
        }
        if (instruction.isReturn())
        {
            return true;
        }
    }

    return false;
}
bool looksLikeIndirectTargetEntry(const std::vector<disasm::Instruction>& disassembled,
                                  const InstructionIndexMap& instructionIndexMap, Address address)
{
    if (looksLikeFunctionEntry(disassembled, instructionIndexMap, address))
    {
        return true;
    }

    auto it = instructionIndexMap.find(address);
    if (it == instructionIndexMap.end())
    {
        return false;
    }

    bool sawNegativeStackAdjust = false;
    bool sawRaSave = false;
    bool sawRaRestore = false;
    const size_t limitIndex = std::min(it->second + 24, disassembled.size());
    for (size_t index = it->second; index < limitIndex; ++index)
    {
        const auto& instruction = disassembled[index];
        if (instruction.opcode == disasm::Opcode::UNKNOWN)
        {
            break;
        }

        if (instruction.opcode == disasm::Opcode::ADDIU && instruction.rs == Registers::SP &&
            instruction.rt == Registers::SP && instruction.immediate < 0)
        {
            sawNegativeStackAdjust = true;
        }
        else if (instruction.opcode == disasm::Opcode::SW && instruction.rs == Registers::SP &&
                 instruction.rt == Registers::RA)
        {
            sawRaSave = true;
        }
        else if (instruction.opcode == disasm::Opcode::LW && instruction.rs == Registers::SP &&
                 instruction.rt == Registers::RA)
        {
            sawRaRestore = true;
        }
        else if (instruction.opcode == disasm::Opcode::JAL ||
                 instruction.opcode == disasm::Opcode::JALR ||
                 instruction.opcode == disasm::Opcode::BGEZAL ||
                 instruction.opcode == disasm::Opcode::BLTZAL)
        {
            return true;
        }

        if ((sawNegativeStackAdjust && (sawRaSave || sawRaRestore)) || (sawRaSave && sawRaRestore))
        {
            return true;
        }
    }

    return false;
}

bool looksLikeCallableCodeRegion(const std::vector<disasm::Instruction>& disassembled,
                                 const InstructionIndexMap& instructionIndexMap, Address address)
{
    auto it = instructionIndexMap.find(address);
    if (it == instructionIndexMap.end())
    {
        return false;
    }

    size_t decodableCount = 0;
    size_t nonNopCount = 0;
    bool sawControlTransfer = false;
    const size_t limitIndex = std::min(it->second + 12, disassembled.size());
    for (size_t index = it->second; index < limitIndex; ++index)
    {
        const auto& instruction = disassembled[index];
        if (instruction.opcode == disasm::Opcode::UNKNOWN)
        {
            break;
        }

        ++decodableCount;
        if (instruction.encoding != 0)
        {
            ++nonNopCount;
        }

        if (instruction.isReturn() || instruction.isBranch() || instruction.isCall() ||
            instruction.opcode == disasm::Opcode::JR)
        {
            sawControlTransfer = true;
        }
    }

    return decodableCount >= 3 && nonNopCount >= 2 && sawControlTransfer;
}

bool isGapAdjacentEntryCandidate(const std::vector<disasm::Instruction>& disassembled,
                                 const InstructionIndexMap& instructionIndexMap,
                                 const std::vector<disasm::FunctionBoundary>& knownBoundaries,
                                 Address address)
{
    auto it = instructionIndexMap.find(address);
    if (it == instructionIndexMap.end() || knownBoundaries.empty())
    {
        return false;
    }

    const disasm::FunctionBoundary* previousBoundary = nullptr;
    const disasm::FunctionBoundary* nextBoundary = nullptr;
    for (const auto& boundary : knownBoundaries)
    {
        if (address >= boundary.start && address <= boundary.end)
        {
            return false;
        }
        if (boundary.end < address)
        {
            previousBoundary = &boundary;
            continue;
        }
        if (boundary.start > address)
        {
            nextBoundary = &boundary;
            break;
        }
    }

    if (previousBoundary == nullptr || nextBoundary == nullptr)
    {
        return false;
    }

    const Address gapStart = previousBoundary->end + 4;
    const Address gapEnd = nextBoundary->start - 4;
    if (gapStart > gapEnd || address != gapStart || address > gapEnd)
    {
        return false;
    }

    constexpr Address kMaxCallableGapBytes = 0x100;
    const Address gapByteSize = gapEnd - gapStart + 4;
    if (gapByteSize > kMaxCallableGapBytes)
    {
        return false;
    }

    size_t decodableCount = 0;
    size_t nonNopCount = 0;
    for (size_t index = it->second; index < disassembled.size(); ++index)
    {
        const auto& instruction = disassembled[index];
        if (instruction.address > gapEnd || instruction.opcode == disasm::Opcode::UNKNOWN)
        {
            break;
        }

        ++decodableCount;
        if (instruction.encoding != 0)
        {
            ++nonNopCount;
        }
    }

    return decodableCount >= 4 && nonNopCount >= 3;
}

bool looksLikeGapAdjacentCallableEntry(const std::vector<disasm::Instruction>& disassembled,
                                       const InstructionIndexMap& instructionIndexMap,
                                       const std::vector<disasm::FunctionBoundary>& knownBoundaries,
                                       Address address)
{
    auto it = instructionIndexMap.find(address);
    if (it == instructionIndexMap.end() ||
        !isGapAdjacentEntryCandidate(disassembled, instructionIndexMap, knownBoundaries, address))
    {
        return false;
    }

    Address gapEnd = 0;
    for (const auto& boundary : knownBoundaries)
    {
        if (boundary.start > address)
        {
            gapEnd = boundary.start - 4;
            break;
        }
    }

    bool sawControlTransfer = false;
    for (size_t index = it->second; index < disassembled.size(); ++index)
    {
        const auto& instruction = disassembled[index];
        if (instruction.address > gapEnd || instruction.opcode == disasm::Opcode::UNKNOWN)
        {
            break;
        }

        if (instruction.isReturn() || instruction.isCall() || instruction.isBranch() ||
            instruction.opcode == disasm::Opcode::JR)
        {
            sawControlTransfer = true;
            break;
        }
    }

    return sawControlTransfer;
}

bool writesRegister(const disasm::Instruction& instruction, Register reg)
{
    switch (instruction.opcode)
    {
    case disasm::Opcode::ADD:
    case disasm::Opcode::ADDU:
    case disasm::Opcode::SUB:
    case disasm::Opcode::SUBU:
    case disasm::Opcode::AND:
    case disasm::Opcode::OR:
    case disasm::Opcode::XOR:
    case disasm::Opcode::NOR:
    case disasm::Opcode::SLT:
    case disasm::Opcode::SLTU:
    case disasm::Opcode::SLL:
    case disasm::Opcode::SRL:
    case disasm::Opcode::SRA:
    case disasm::Opcode::SLLV:
    case disasm::Opcode::SRLV:
    case disasm::Opcode::SRAV:
    case disasm::Opcode::MFHI:
    case disasm::Opcode::MFLO:
    case disasm::Opcode::JALR:
        return instruction.rd == reg;
    case disasm::Opcode::ADDI:
    case disasm::Opcode::ADDIU:
    case disasm::Opcode::ANDI:
    case disasm::Opcode::ORI:
    case disasm::Opcode::XORI:
    case disasm::Opcode::SLTI:
    case disasm::Opcode::SLTIU:
    case disasm::Opcode::LUI:
    case disasm::Opcode::LB:
    case disasm::Opcode::LH:
    case disasm::Opcode::LW:
    case disasm::Opcode::LBU:
    case disasm::Opcode::LHU:
    case disasm::Opcode::LWL:
    case disasm::Opcode::LWR:
    case disasm::Opcode::MFC0:
    case disasm::Opcode::CFC0:
    case disasm::Opcode::MFC2:
    case disasm::Opcode::CFC2:
        return instruction.rt == reg;
    case disasm::Opcode::JAL:
    case disasm::Opcode::BLTZAL:
    case disasm::Opcode::BGEZAL:
        return reg == Registers::RA;
    default:
        return false;
    }
}

std::unordered_set<Address>
findReferencedDataWords(const std::vector<disasm::Instruction>& disassembled,
                        const disasm::CodeDataSegmentation& segmentation, Address baseAddress,
                        Address endAddress)
{
    std::unordered_set<Address> referencedWords;
    for (size_t index = 0; index < disassembled.size(); ++index)
    {
        const auto& instruction = disassembled[index];
        if (instruction.opcode != disasm::Opcode::LUI ||
            !isAddressInRanges(segmentation.codeRanges, instruction.address))
        {
            continue;
        }

        const u8 baseRegister = instruction.rt;
        const u32 hiImm = instruction.immediate & 0xFFFFu;
        std::optional<Address> builtAddress;
        size_t builtAddressIndex = 0;
        for (size_t lookAhead = 1; lookAhead <= 6 && index + lookAhead < disassembled.size();
             ++lookAhead)
        {
            const auto& next = disassembled[index + lookAhead];
            if (next.opcode == disasm::Opcode::ADDIU && next.rs == baseRegister &&
                next.rt == baseRegister)
            {
                const u32 lo = next.immediate & 0xFFFFu;
                const s32 signedLo =
                    (lo & 0x8000u) ? static_cast<s32>(lo | 0xFFFF0000u) : static_cast<s32>(lo);
                builtAddress = static_cast<Address>((hiImm << 16) + static_cast<u32>(signedLo));
                builtAddressIndex = index + lookAhead;
                break;
            }
            if (next.opcode == disasm::Opcode::ORI && next.rs == baseRegister &&
                next.rt == baseRegister)
            {
                builtAddress = static_cast<Address>((hiImm << 16) | (next.immediate & 0xFFFFu));
                builtAddressIndex = index + lookAhead;
                break;
            }
            if (writesRegister(next, baseRegister))
            {
                break;
            }
        }

        if (!builtAddress.has_value())
        {
            for (size_t lookAhead = 1; lookAhead <= 6 && index + lookAhead < disassembled.size();
                 ++lookAhead)
            {
                const auto& next = disassembled[index + lookAhead];
                const bool isMemoryReference =
                    next.rs == baseRegister &&
                    (next.opcode == disasm::Opcode::LW || next.opcode == disasm::Opcode::SW ||
                     next.opcode == disasm::Opcode::LWL || next.opcode == disasm::Opcode::LWR ||
                     next.opcode == disasm::Opcode::SWL || next.opcode == disasm::Opcode::SWR);
                if (isMemoryReference)
                {
                    const Address referencedAddress = static_cast<Address>(
                        (hiImm << 16) + static_cast<u32>(static_cast<s32>(next.immediate)));
                    if (referencedAddress >= baseAddress && referencedAddress + 3 < endAddress &&
                        (referencedAddress % 4) == 0 &&
                        isAddressInRanges(segmentation.dataRanges, referencedAddress))
                    {
                        referencedWords.insert(referencedAddress);
                    }
                }
                if (writesRegister(next, baseRegister))
                {
                    break;
                }
            }
            continue;
        }

        for (size_t lookAhead = 1;
             lookAhead <= 12 && builtAddressIndex + lookAhead < disassembled.size(); ++lookAhead)
        {
            const auto& next = disassembled[builtAddressIndex + lookAhead];
            const bool isMemoryReference =
                next.rs == baseRegister &&
                (next.opcode == disasm::Opcode::LW || next.opcode == disasm::Opcode::SW ||
                 next.opcode == disasm::Opcode::LWL || next.opcode == disasm::Opcode::LWR ||
                 next.opcode == disasm::Opcode::SWL || next.opcode == disasm::Opcode::SWR);
            if (isMemoryReference)
            {
                const Address referencedAddress =
                    static_cast<Address>(*builtAddress + static_cast<s32>(next.immediate));
                if (referencedAddress >= baseAddress && referencedAddress + 3 < endAddress &&
                    (referencedAddress % 4) == 0 &&
                    isAddressInRanges(segmentation.dataRanges, referencedAddress))
                {
                    referencedWords.insert(referencedAddress);
                }
            }
            if (writesRegister(next, baseRegister))
            {
                break;
            }
        }
    }

    return referencedWords;
}

bool isDataLikeEntryPoint(const std::vector<disasm::Instruction>& disassembled,
                           const InstructionIndexMap& instructionIndexMap, Address address)
{
    auto it = instructionIndexMap.find(address);
    if (it == instructionIndexMap.end()) { return false; }
    const auto op = disassembled[it->second].opcode;
    return op == disasm::Opcode::TEQ || op == disasm::Opcode::TGE || op == disasm::Opcode::TGEU
        || op == disasm::Opcode::TLT || op == disasm::Opcode::TLTU || op == disasm::Opcode::TNE
        || op == disasm::Opcode::TEQI || op == disasm::Opcode::TNEI;
}

void appendUniqueSorted(std::vector<Address>& values, Address value)
{
    if (std::find(values.begin(), values.end(), value) == values.end())
    {
        values.push_back(value);
    }
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
