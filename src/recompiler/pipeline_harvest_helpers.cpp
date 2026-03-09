#include "pipeline_harvest_helpers.h"

#include "pipeline_harvest_internal.h"

#include "../disasm/analysis_helpers.h"

#include <algorithm>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{
PointerHarvestResults harvestFunctionPointerSeeds(
    const std::vector<disasm::Instruction>& disassembled, const iso::PsxExeImage& exeImage,
    const disasm::CodeDataSegmentation& segmentation, const std::vector<Address>& entrySeeds,
    const std::vector<disasm::JumpTableInfo>& jumpTables, Address baseAddress)
{
    PointerHarvestResults results;
    const InstructionIndexMap instructionIndexMap =
        disasm::detail::buildInstructionIndex(disassembled);
    const Address endAddress = baseAddress + static_cast<Address>(exeImage.programData.size());
    const auto referencedDataWords =
        findReferencedDataWords(disassembled, segmentation, baseAddress, endAddress);
    std::unordered_set<Address> existingSeeds(entrySeeds.begin(), entrySeeds.end());
    std::unordered_set<Address> knownPointerWords;
    const auto scoreSeedCandidate =
        [&](Address wordAddress, Address target, bool referencedWord, bool hasNearbyCodePointer)
    {
        int score = 0;
        if (referencedWord)
        {
            score += 4;
        }
        if (hasNearbyCodePointer)
        {
            score += 3;
        }
        if (isAddressInRanges(segmentation.codeRanges, target))
        {
            score += 2;
        }
        if (looksLikeFunctionEntry(disassembled, instructionIndexMap, target))
        {
            score += 4;
        }
        else if (looksLikeIndirectTargetEntry(disassembled, instructionIndexMap, target))
        {
            score += 3;
        }
        else if (looksLikeCallableCodeRegion(disassembled, instructionIndexMap, target))
        {
            score += 2;
        }

        if (referencedWord || hasNearbyCodePointer)
        {
            const Address nextWord = wordAddress + 4;
            if (nextWord + 3 < endAddress)
            {
                const Address neighborTarget = static_cast<Address>(
                    readProgramWord(exeImage.programData, baseAddress, nextWord));
                if (neighborTarget >= baseAddress && neighborTarget < endAddress &&
                    (neighborTarget % 4) == 0 &&
                    isDecodableInstruction(disassembled, instructionIndexMap, neighborTarget))
                {
                    score += 1;
                }
            }
        }

        return score;
    };

    {
        std::vector<Address> clusteredCodePointers;
        const auto flushClusteredCodePointers = [&]()
        {
            if (clusteredCodePointers.size() >= 2)
            {
                std::vector<Address> anchoredTargets;
                anchoredTargets.reserve(clusteredCodePointers.size());
                for (const Address target : clusteredCodePointers)
                {
                    if (!isDecodableInstruction(disassembled, instructionIndexMap, target))
                    {
                        continue;
                    }
                    if (looksLikeFunctionEntry(disassembled, instructionIndexMap, target) ||
                        isAddressInRanges(segmentation.codeRanges, target))
                    {
                        anchoredTargets.push_back(target);
                    }
                }

                if (!anchoredTargets.empty())
                {
                    constexpr size_t kMaxAnchorInstructionDistance = 16;
                    const auto sharesCodeNeighborhood =
                        [&instructionIndexMap, &disassembled](Address lhs, Address rhs)
                    {
                        auto lhsIt = instructionIndexMap.find(lhs);
                        auto rhsIt = instructionIndexMap.find(rhs);
                        if (lhsIt == instructionIndexMap.end() ||
                            rhsIt == instructionIndexMap.end())
                        {
                            return false;
                        }

                        const size_t minIndex = std::min(lhsIt->second, rhsIt->second);
                        const size_t maxIndex = std::max(lhsIt->second, rhsIt->second);
                        if (maxIndex - minIndex > kMaxAnchorInstructionDistance)
                        {
                            return false;
                        }

                        for (size_t index = minIndex; index <= maxIndex; ++index)
                        {
                            if (disassembled[index].opcode == disasm::Opcode::UNKNOWN)
                            {
                                return false;
                            }
                        }

                        return true;
                    };

                    for (const Address target : clusteredCodePointers)
                    {
                        if (!isDecodableInstruction(disassembled, instructionIndexMap, target))
                        {
                            continue;
                        }

                        if (looksLikeFunctionEntry(disassembled, instructionIndexMap, target) ||
                            isAddressInRanges(segmentation.codeRanges, target) ||
                            std::any_of(anchoredTargets.begin(), anchoredTargets.end(),
                                        [target, &sharesCodeNeighborhood](Address anchor)
                                        { return sharesCodeNeighborhood(target, anchor); }))
                        {
                            appendUniqueSorted(results.harvestedPointers, target);
                        }
                    }
                }
            }
            else if (clusteredCodePointers.size() == 1)
            {
                const Address target = clusteredCodePointers.front();
                if (isDecodableInstruction(disassembled, instructionIndexMap, target) &&
                    (looksLikeFunctionEntry(disassembled, instructionIndexMap, target) ||
                     looksLikeIndirectTargetEntry(disassembled, instructionIndexMap, target) ||
                     looksLikeCallableCodeRegion(disassembled, instructionIndexMap, target)))
                {
                    appendUniqueSorted(results.harvestedPointers, target);
                }
            }
            clusteredCodePointers.clear();
        };

        for (Address address = baseAddress; address + 3 < endAddress; address += 4)
        {
            const u32 value = readProgramWord(exeImage.programData, baseAddress, address);
            if (value >= baseAddress && value < endAddress && (value % 4) == 0 &&
                instructionIndexMap.count(value) != 0)
            {
                clusteredCodePointers.push_back(static_cast<Address>(value));
            }
            else
            {
                flushClusteredCodePointers();
            }
        }
        flushClusteredCodePointers();
    }

    constexpr size_t kMaxJumpTableEntries = 64;
    for (const auto& jumpTable : jumpTables)
    {
        if (!isAddressInRanges(segmentation.codeRanges, jumpTable.jumpAddress) ||
            !jumpTable.tableBaseAddress.has_value())
        {
            continue;
        }

        Address tableAddress =
            static_cast<Address>(*jumpTable.tableBaseAddress + jumpTable.tableOffset);
        for (size_t entryIndex = 0; entryIndex < kMaxJumpTableEntries; ++entryIndex)
        {
            if (tableAddress < baseAddress || tableAddress + 3 >= endAddress)
            {
                break;
            }

            const Address target = static_cast<Address>(
                readProgramWord(exeImage.programData, baseAddress, tableAddress));
            if (target < baseAddress || target >= endAddress || (target % 4) != 0 ||
                instructionIndexMap.count(target) == 0 ||
                !isDecodableInstruction(disassembled, instructionIndexMap, target))
            {
                break;
            }

            if (existingSeeds.insert(target).second)
            {
                results.jumpTableHarvestedPointers.push_back(target);
            }
            knownPointerWords.insert(tableAddress);
            tableAddress += 4;
        }
    }

    for (Address address = baseAddress; address + 3 < endAddress; address += 4)
    {
        const Address target =
            static_cast<Address>(readProgramWord(exeImage.programData, baseAddress, address));
        if (target < baseAddress || target >= endAddress || (target % 4) != 0 ||
            !isDecodableInstruction(disassembled, instructionIndexMap, target) ||
            existingSeeds.find(target) != existingSeeds.end())
        {
            continue;
        }

        bool hasNearbyCodePointer = false;
        for (s32 delta = -8; delta <= 8; delta += 4)
        {
            if (delta == 0)
            {
                continue;
            }

            const s32 signedNeighbor = static_cast<s32>(address) + delta;
            if (signedNeighbor < static_cast<s32>(baseAddress))
            {
                continue;
            }

            const Address neighborAddress = static_cast<Address>(signedNeighbor);
            if (neighborAddress + 3 >= endAddress)
            {
                continue;
            }

            const Address neighborTarget =
                static_cast<Address>(readProgramWord(exeImage.programData, baseAddress,
                                                    neighborAddress));
            if (neighborTarget >= baseAddress && neighborTarget < endAddress &&
                (neighborTarget % 4) == 0 &&
                isDecodableInstruction(disassembled, instructionIndexMap, neighborTarget))
            {
                hasNearbyCodePointer = true;
                break;
            }
        }

        const bool referencedWord = referencedDataWords.find(address) != referencedDataWords.end();
        const int candidateScore =
            scoreSeedCandidate(address, target, referencedWord, hasNearbyCodePointer);
        if (candidateScore < 6)
        {
            continue;
        }

        if (!referencedWord && !hasNearbyCodePointer &&
            !looksLikeFunctionEntry(disassembled, instructionIndexMap, target) &&
            !looksLikeIndirectTargetEntry(disassembled, instructionIndexMap, target) &&
            !isAddressInRanges(segmentation.codeRanges, target))
        {
            continue;
        }

        if (existingSeeds.insert(target).second)
        {
            results.harvestedPointers.push_back(target);
        }
        knownPointerWords.insert(address);
    }

    size_t harvestedFromCode = 0;
    for (size_t index = 0; index < disassembled.size(); ++index)
    {
        const auto& instruction = disassembled[index];
        if (instruction.opcode != disasm::Opcode::LUI)
        {
            continue;
        }

        const u8 addressRegister = instruction.rt;
        const u32 hiImm = instruction.immediate & 0xFFFFu;
        std::optional<Address> builtAddress;
        size_t builtAddressIndex = 0;
        for (size_t lookAhead = 1; lookAhead <= 6 && index + lookAhead < disassembled.size();
             ++lookAhead)
        {
            const auto& next = disassembled[index + lookAhead];
            if (next.opcode == disasm::Opcode::ADDIU && next.rs == addressRegister &&
                next.rt == addressRegister)
            {
                const u32 lo = next.immediate & 0xFFFFu;
                const s32 signedLo =
                    (lo & 0x8000u) ? static_cast<s32>(lo | 0xFFFF0000u) : static_cast<s32>(lo);
                builtAddress = static_cast<Address>((hiImm << 16) + static_cast<u32>(signedLo));
                builtAddressIndex = index + lookAhead;
                break;
            }
            if (next.opcode == disasm::Opcode::ORI && next.rs == addressRegister &&
                next.rt == addressRegister)
            {
                builtAddress = static_cast<Address>((hiImm << 16) | (next.immediate & 0xFFFFu));
                builtAddressIndex = index + lookAhead;
                break;
            }
            if (writesRegister(next, addressRegister))
            {
                break;
            }
        }

        if (!builtAddress.has_value())
        {
            continue;
        }

        bool usedAsLikelyCodeTarget = false;
        for (size_t lookAhead = 1;
             lookAhead <= 16 && builtAddressIndex + lookAhead < disassembled.size(); ++lookAhead)
        {
            const auto& next = disassembled[builtAddressIndex + lookAhead];
            if ((next.opcode == disasm::Opcode::JR || next.opcode == disasm::Opcode::JALR) &&
                next.rs == addressRegister)
            {
                usedAsLikelyCodeTarget = true;
                break;
            }
            if ((next.opcode == disasm::Opcode::SW || next.opcode == disasm::Opcode::SWL ||
                 next.opcode == disasm::Opcode::SWR) &&
                next.rt == addressRegister)
            {
                usedAsLikelyCodeTarget = true;
                break;
            }
            if (next.opcode == disasm::Opcode::JAL && addressRegister >= Registers::A0 &&
                addressRegister <= Registers::A3)
            {
                usedAsLikelyCodeTarget = true;
                break;
            }
            if (writesRegister(next, addressRegister))
            {
                break;
            }
        }

        const Address target = builtAddress.value();
        if (usedAsLikelyCodeTarget && target >= baseAddress && target < endAddress &&
            (target % 4) == 0 &&
            (looksLikeFunctionEntry(disassembled, instructionIndexMap, target) ||
             looksLikeCallableCodeRegion(disassembled, instructionIndexMap, target)) &&
            existingSeeds.find(target) == existingSeeds.end())
        {
            results.codeHarvestedPointers.push_back(target);
            existingSeeds.insert(target);
            ++harvestedFromCode;
        }
    }
    (void)harvestedFromCode;

    results.knownPointerTableWords.assign(knownPointerWords.begin(), knownPointerWords.end());
    std::sort(results.harvestedPointers.begin(), results.harvestedPointers.end());
    std::sort(results.jumpTableHarvestedPointers.begin(), results.jumpTableHarvestedPointers.end());
    std::sort(results.codeHarvestedPointers.begin(), results.codeHarvestedPointers.end());
    std::sort(results.knownPointerTableWords.begin(), results.knownPointerTableWords.end());
    results.harvestedPointers.erase(
        std::unique(results.harvestedPointers.begin(), results.harvestedPointers.end()),
        results.harvestedPointers.end());
    results.jumpTableHarvestedPointers.erase(std::unique(results.jumpTableHarvestedPointers.begin(),
                                                         results.jumpTableHarvestedPointers.end()),
                                             results.jumpTableHarvestedPointers.end());
    results.codeHarvestedPointers.erase(
        std::unique(results.codeHarvestedPointers.begin(), results.codeHarvestedPointers.end()),
        results.codeHarvestedPointers.end());
    results.knownPointerTableWords.erase(
        std::unique(results.knownPointerTableWords.begin(), results.knownPointerTableWords.end()),
        results.knownPointerTableWords.end());
    return results;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
