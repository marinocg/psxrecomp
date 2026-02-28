#include "pipeline_analysis_helpers.h"

#include "psxrecomp/disasm/analysis.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

PipelineCodeLayout analyzeCodeLayout(const std::vector<disasm::Instruction>& disassembled,
                                     const iso::PsxExeImage& exeImage, Address baseAddress,
                                     Address entryAddress)
{
    PipelineCodeLayout layout;

    const auto jumpTables = disasm::findJumpTables(disassembled);
    std::unordered_map<Address, disasm::Opcode> opcodeByAddress;
    opcodeByAddress.reserve(disassembled.size());
    for (const auto& instruction : disassembled)
    {
        opcodeByAddress.emplace(instruction.address, instruction.opcode);
    }
    const auto hasKnownOpcode = [&opcodeByAddress](Address address)
    {
        auto it = opcodeByAddress.find(address);
        return it != opcodeByAddress.end() && it->second != disasm::Opcode::UNKNOWN;
    };

    const Address binaryEnd = baseAddress + static_cast<Address>(exeImage.programData.size());
    std::vector<Address> entrySeeds = {entryAddress};
    if (baseAddress != entryAddress)
    {
        entrySeeds.push_back(baseAddress);
    }
    for (const auto& instruction : disassembled)
    {
        if (instruction.opcode == disasm::Opcode::JAL ||
            instruction.opcode == disasm::Opcode::BGEZAL ||
            instruction.opcode == disasm::Opcode::BLTZAL)
        {
            auto target = instruction.getTargetAddress();
            if (target.has_value() && *target >= baseAddress && *target < binaryEnd &&
                (*target % 4) == 0)
            {
                entrySeeds.push_back(*target);
            }
        }
    }

    layout.segmentation = disasm::segmentCodeAndData(disassembled, entrySeeds, jumpTables);

    std::vector<Address> harvestedPointers;
    {
        const Address endAddress = baseAddress + static_cast<Address>(exeImage.programData.size());
        for (const auto& range : layout.segmentation.dataRanges)
        {
            for (Address addr = range.start; addr <= range.end; addr += 4)
            {
                const size_t offset = addr - baseAddress;
                if (offset + 4 <= exeImage.programData.size())
                {
                    const uint32_t value =
                        static_cast<uint32_t>(exeImage.programData[offset]) |
                        (static_cast<uint32_t>(exeImage.programData[offset + 1]) << 8) |
                        (static_cast<uint32_t>(exeImage.programData[offset + 2]) << 16) |
                        (static_cast<uint32_t>(exeImage.programData[offset + 3]) << 24);
                    if (value >= baseAddress && value < endAddress && (value % 4) == 0 &&
                        hasKnownOpcode(value))
                    {
                        harvestedPointers.push_back(value);
                    }
                }
            }
        }
        if (!harvestedPointers.empty())
        {
            for (const Address ptr : harvestedPointers)
            {
                entrySeeds.push_back(ptr);
            }
            layout.segmentation = disasm::segmentCodeAndData(disassembled, entrySeeds, jumpTables);
        }
    }

    std::vector<Address> codeHarvestedPointers;
    {
        const Address endAddress = baseAddress + static_cast<Address>(exeImage.programData.size());
        std::unordered_set<Address> existingSeeds(entrySeeds.begin(), entrySeeds.end());
        const auto writesRegister = [](const disasm::Instruction& instruction, Register reg)
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
            case disasm::Opcode::LWC0:
            case disasm::Opcode::LWC2:
                return instruction.rt == reg;
            case disasm::Opcode::JAL:
            case disasm::Opcode::BLTZAL:
            case disasm::Opcode::BGEZAL:
                return reg == Registers::RA;
            default:
                return false;
            }
        };
        size_t harvestedFromCode = 0;

        for (size_t i = 0; i < disassembled.size(); ++i)
        {
            const auto& inst = disassembled[i];
            if (inst.opcode != disasm::Opcode::LUI)
            {
                continue;
            }
            const u8 luiReg = inst.rt;
            const u32 hiImm = inst.immediate & 0xFFFFu;
            std::optional<Address> builtAddress;
            size_t builtAddressIndex = 0;

            for (size_t j = 1; j <= 6 && i + j < disassembled.size(); ++j)
            {
                const auto& next = disassembled[i + j];
                if (next.opcode == disasm::Opcode::ADDIU && next.rs == luiReg && next.rt == luiReg)
                {
                    const u32 lo = next.immediate & 0xFFFFu;
                    const s32 slo =
                        (lo & 0x8000u) ? static_cast<s32>(lo | 0xFFFF0000u) : static_cast<s32>(lo);
                    builtAddress = static_cast<Address>((hiImm << 16) + static_cast<u32>(slo));
                    builtAddressIndex = i + j;
                    break;
                }
                if (next.opcode == disasm::Opcode::ORI && next.rs == luiReg && next.rt == luiReg)
                {
                    const u32 lo = next.immediate & 0xFFFFu;
                    builtAddress = static_cast<Address>((hiImm << 16) | lo);
                    builtAddressIndex = i + j;
                    break;
                }
                if (writesRegister(next, luiReg))
                {
                    break;
                }
            }

            if (!builtAddress.has_value())
            {
                continue;
            }

            bool usedAsLikelyCodeTarget = false;
            for (size_t j = 1; j <= 16 && builtAddressIndex + j < disassembled.size(); ++j)
            {
                const auto& next = disassembled[builtAddressIndex + j];
                if ((next.opcode == disasm::Opcode::JR || next.opcode == disasm::Opcode::JALR) &&
                    next.rs == luiReg)
                {
                    usedAsLikelyCodeTarget = true;
                    break;
                }
                if ((next.opcode == disasm::Opcode::SW || next.opcode == disasm::Opcode::SWL ||
                     next.opcode == disasm::Opcode::SWR) &&
                    next.rt == luiReg)
                {
                    usedAsLikelyCodeTarget = true;
                    break;
                }
                if (next.opcode == disasm::Opcode::JAL && luiReg >= Registers::A0 &&
                    luiReg <= Registers::A3)
                {
                    usedAsLikelyCodeTarget = true;
                    break;
                }
                if (writesRegister(next, luiReg))
                {
                    break;
                }
            }

            const Address addr = builtAddress.value();
            if (usedAsLikelyCodeTarget && addr >= baseAddress && addr < endAddress &&
                (addr % 4) == 0 && hasKnownOpcode(addr) &&
                existingSeeds.find(addr) == existingSeeds.end())
            {
                entrySeeds.push_back(addr);
                existingSeeds.insert(addr);
                codeHarvestedPointers.push_back(addr);
                ++harvestedFromCode;
            }
        }

        if (harvestedFromCode > 0)
        {
            layout.segmentation = disasm::segmentCodeAndData(disassembled, entrySeeds, jumpTables);
        }
    }

    {
        std::unordered_set<Address> codeAddresses;
        for (const auto& range : layout.segmentation.codeRanges)
        {
            for (Address addr = range.start; addr <= range.end; addr += 4)
            {
                codeAddresses.insert(addr);
            }
        }
        layout.codeInstructions.reserve(codeAddresses.size());
        for (const auto& instruction : disassembled)
        {
            if (codeAddresses.count(instruction.address))
            {
                layout.codeInstructions.push_back(instruction);
            }
        }
    }

    if (layout.codeInstructions.empty())
    {
        return layout;
    }

    std::vector<Address> additionalStarts = harvestedPointers;
    additionalStarts.insert(additionalStarts.end(), codeHarvestedPointers.begin(),
                            codeHarvestedPointers.end());
    additionalStarts.push_back(entryAddress);

    layout.boundaries = disasm::findFunctionBoundaries(layout.codeInstructions, additionalStarts);
    if (layout.boundaries.empty())
    {
        layout.boundaries.push_back(
            {entryAddress, layout.codeInstructions.back().address, false, false});
    }

    bool entryFound = false;
    for (const auto& boundary : layout.boundaries)
    {
        if (boundary.start == entryAddress)
        {
            entryFound = true;
            break;
        }
    }
    if (!entryFound)
    {
        layout.boundaries.push_back({entryAddress, entryAddress, false, false});
    }

    std::sort(
        layout.boundaries.begin(), layout.boundaries.end(),
        [entryAddress](const disasm::FunctionBoundary& lhs, const disasm::FunctionBoundary& rhs)
        {
            const bool lhsIsEntry = lhs.start == entryAddress;
            const bool rhsIsEntry = rhs.start == entryAddress;
            if (lhsIsEntry != rhsIsEntry)
            {
                return lhsIsEntry;
            }
            return lhs.start < rhs.start;
        });

    std::vector<disasm::FunctionBoundary> filledBoundaries;
    filledBoundaries.reserve(layout.boundaries.size() * 2);

    for (size_t i = 0; i < layout.boundaries.size(); ++i)
    {
        filledBoundaries.push_back(layout.boundaries[i]);

        if (i + 1 < layout.boundaries.size())
        {
            const Address gapStart = layout.boundaries[i].end + 4;
            const Address gapEnd = layout.boundaries[i + 1].start - 4;
            if (gapStart <= gapEnd)
            {
                filledBoundaries.push_back({gapStart, gapEnd, false, false});
            }
        }
    }

    if (!filledBoundaries.empty() && !layout.codeInstructions.empty())
    {
        const Address lastEnd = filledBoundaries.back().end;
        const Address codeEnd = layout.codeInstructions.back().address;
        if (lastEnd + 4 <= codeEnd)
        {
            filledBoundaries.push_back({lastEnd + 4, codeEnd, false, false});
        }
    }

    layout.boundaries = std::move(filledBoundaries);
    return layout;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
