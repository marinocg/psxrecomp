#include "pipeline_analysis_helpers.h"

#include "pipeline_harvest_helpers.h"
#include "psxrecomp/disasm/analysis.h"

#include <algorithm>
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
    const Address binaryEnd = baseAddress + static_cast<Address>(exeImage.programData.size());
    std::vector<Address> entrySeeds = {entryAddress};
    if (baseAddress != entryAddress)
    {
        entrySeeds.push_back(baseAddress);
    }

    layout.segmentation = disasm::segmentCodeAndData(disassembled, entrySeeds, jumpTables);

    const auto isSegmentedCodeAddress = [&layout](Address address)
    {
        return std::any_of(layout.segmentation.codeRanges.begin(),
                           layout.segmentation.codeRanges.end(),
                           [address](const disasm::AddressRange& range)
                           { return address >= range.start && address <= range.end; });
    };

    {
        std::unordered_set<Address> existingSeeds(entrySeeds.begin(), entrySeeds.end());
        bool addedReachableCallTarget = false;
        for (const auto& instruction : disassembled)
        {
            if (!isSegmentedCodeAddress(instruction.address))
            {
                continue;
            }

            if (instruction.opcode != disasm::Opcode::JAL &&
                instruction.opcode != disasm::Opcode::BGEZAL &&
                instruction.opcode != disasm::Opcode::BLTZAL)
            {
                continue;
            }

            auto target = instruction.getTargetAddress();
            if (!target.has_value() || *target < baseAddress || *target >= binaryEnd ||
                (*target % 4) != 0)
            {
                continue;
            }

            if (existingSeeds.insert(*target).second)
            {
                entrySeeds.push_back(*target);
                addedReachableCallTarget = true;
            }
        }

        if (addedReachableCallTarget)
        {
            layout.segmentation = disasm::segmentCodeAndData(disassembled, entrySeeds, jumpTables);
        }
    }

    PointerHarvestResults harvestedResults;
    const auto mergeUnique =
        [](std::vector<Address>& destination, const std::vector<Address>& source)
    {
        destination.insert(destination.end(), source.begin(), source.end());
        std::sort(destination.begin(), destination.end());
        destination.erase(std::unique(destination.begin(), destination.end()), destination.end());
    };

    for (size_t pass = 0; pass < 4; ++pass)
    {
        const PointerHarvestResults passResults = harvestFunctionPointerSeeds(
            disassembled, exeImage, layout.segmentation, entrySeeds, jumpTables, baseAddress);
        mergeUnique(harvestedResults.harvestedPointers, passResults.harvestedPointers);
        mergeUnique(harvestedResults.jumpTableHarvestedPointers,
                    passResults.jumpTableHarvestedPointers);
        mergeUnique(harvestedResults.codeHarvestedPointers, passResults.codeHarvestedPointers);
        mergeUnique(harvestedResults.knownPointerTableWords, passResults.knownPointerTableWords);

        std::unordered_set<Address> existingSeeds(entrySeeds.begin(), entrySeeds.end());
        bool addedSeeds = false;
        const auto appendSeeds = [&](const std::vector<Address>& seeds)
        {
            for (const Address target : seeds)
            {
                if (existingSeeds.insert(target).second)
                {
                    entrySeeds.push_back(target);
                    addedSeeds = true;
                }
            }
        };

        appendSeeds(passResults.jumpTableHarvestedPointers);
        appendSeeds(passResults.harvestedPointers);
        appendSeeds(passResults.codeHarvestedPointers);
        if (!addedSeeds)
        {
            break;
        }

        layout.segmentation = disasm::segmentCodeAndData(disassembled, entrySeeds, jumpTables);
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

    std::unordered_set<Address> delaySlotAddresses;
    for (const auto& instr : layout.codeInstructions)
    {
        if (instr.hasDelaySlot())
        {
            delaySlotAddresses.insert(instr.address + 4);
        }
    }

    std::vector<Address> additionalStarts = harvestedResults.harvestedPointers;
    additionalStarts.insert(additionalStarts.end(),
                            harvestedResults.jumpTableHarvestedPointers.begin(),
                            harvestedResults.jumpTableHarvestedPointers.end());
    additionalStarts.insert(additionalStarts.end(), harvestedResults.codeHarvestedPointers.begin(),
                            harvestedResults.codeHarvestedPointers.end());
    additionalStarts.push_back(entryAddress);
    additionalStarts.erase(
        std::remove_if(additionalStarts.begin(), additionalStarts.end(),
                       [&delaySlotAddresses](Address a)
                       { return delaySlotAddresses.count(a) != 0; }),
        additionalStarts.end());

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
    const auto rangeHasCodeInstruction = [&layout](Address start, Address end)
    {
        if (start > end)
        {
            return false;
        }

        auto it = std::lower_bound(layout.codeInstructions.begin(), layout.codeInstructions.end(),
                                   start, [](const disasm::Instruction& instruction, Address target)
                                   { return instruction.address < target; });
        return it != layout.codeInstructions.end() && it->address <= end;
    };
    const auto extendIfDelaySlot = [&layout](disasm::FunctionBoundary& b)
    {
        const auto it = std::lower_bound(
            layout.codeInstructions.begin(), layout.codeInstructions.end(), b.end,
            [](const disasm::Instruction& i, Address a) { return i.address < a; });
        if (it != layout.codeInstructions.end() && it->address == b.end && it->hasDelaySlot())
        {
            b.end = b.end + 4;
        }
    };

    for (size_t i = 0; i < layout.boundaries.size(); ++i)
    {
        filledBoundaries.push_back(layout.boundaries[i]);
        extendIfDelaySlot(filledBoundaries.back());

        if (i + 1 < layout.boundaries.size())
        {
            const Address gapStart = filledBoundaries.back().end + 4;
            const Address gapEnd = layout.boundaries[i + 1].start - 4;
            if (rangeHasCodeInstruction(gapStart, gapEnd))
            {
                filledBoundaries.push_back({gapStart, gapEnd, false, false});
            }
        }
    }

    if (!filledBoundaries.empty() && !layout.codeInstructions.empty())
    {
        extendIfDelaySlot(filledBoundaries.back());
        const Address lastEnd = filledBoundaries.back().end;
        const Address codeEnd = layout.codeInstructions.back().address;
        if (rangeHasCodeInstruction(lastEnd + 4, codeEnd))
        {
            filledBoundaries.push_back({lastEnd + 4, codeEnd, false, false});
        }
    }

    layout.boundaries = std::move(filledBoundaries);
    layout.speculativeFunctionEntries = harvestedResults.harvestedPointers;
    layout.harvestedFunctionEntries = entrySeeds;
    std::sort(layout.harvestedFunctionEntries.begin(), layout.harvestedFunctionEntries.end());
    layout.harvestedFunctionEntries.erase(
        std::unique(layout.harvestedFunctionEntries.begin(), layout.harvestedFunctionEntries.end()),
        layout.harvestedFunctionEntries.end());
    layout.knownPointerTableWords = harvestedResults.knownPointerTableWords;
    layout.indirectCallSites =
        collectIndirectCallSiteMetadata(layout.codeInstructions, layout.boundaries);
    return layout;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
