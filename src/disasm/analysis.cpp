#include "psxrecomp/disasm/analysis.h"

#include "analysis_helpers.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace disasm
{

namespace
{
using InstructionIndexMap = std::unordered_map<Address, size_t>;

std::unordered_set<Address>
collectFunctionStartAddresses(const std::vector<Instruction>& instructions,
                              const InstructionIndexMap& indexMap,
                              const std::vector<Address>* additionalStarts)
{
    std::unordered_set<Address> startAddresses;
    startAddresses.insert(instructions.front().address);

    for (size_t i = 0; i < instructions.size(); ++i)
    {
        if (detail::hasProloguePattern(instructions, i))
        {
            startAddresses.insert(instructions[i].address);
        }

        if (detail::isDirectCall(instructions[i]))
        {
            if (auto target = detail::resolveDirectCallTarget(instructions[i]))
            {
                // Only add targets that correspond to an actual instruction
                // in the stream. After code/data segmentation, some JAL
                // targets may point into data regions and must be skipped.
                if (indexMap.count(*target))
                {
                    startAddresses.insert(*target);
                }
            }
        }
    }

    if (additionalStarts != nullptr)
    {
        for (Address addr : *additionalStarts)
        {
            if (indexMap.count(addr))
            {
                startAddresses.insert(addr);
            }
        }
    }

    return startAddresses;
}

void mergeConsecutiveFunctionSplits(const std::vector<Instruction>& instructions,
                                    const InstructionIndexMap& indexMap,
                                    std::unordered_set<Address>& startAddresses,
                                    const std::unordered_set<Address>* pinnedAddresses = nullptr)
{
    std::unordered_set<Address> directCallTargets;
    directCallTargets.reserve(instructions.size() / 8);
    for (const auto& instruction : instructions)
    {
        if (!detail::isDirectCall(instruction))
        {
            continue;
        }
        if (auto target = detail::resolveDirectCallTarget(instruction))
        {
            directCallTargets.insert(*target);
        }
    }

    std::vector<Address> tempStarts(startAddresses.begin(), startAddresses.end());
    std::sort(tempStarts.begin(), tempStarts.end());

    bool merged = true;
    while (merged)
    {
        merged = false;
        for (size_t si = 0; si + 1 < tempStarts.size(); ++si)
        {
            const Address funcStart = tempStarts[si];
            const Address nextFuncStart = tempStarts[si + 1];

            auto startIt = indexMap.find(funcStart);
            auto nextIt = indexMap.find(nextFuncStart);
            if (startIt == indexMap.end() || nextIt == indexMap.end())
            {
                continue;
            }

            const size_t startIdx = startIt->second;
            const size_t limitIdx = nextIt->second;

            // Check 1: fall-through — no hard terminator in
            // [funcStart, nextFuncStart).  Branches are excluded here because
            // they still have a fallthrough path and are common in short
            // PSn00bSDK wrapper stubs that should merge with the next chunk.
            bool hasHardTerminator = false;
            bool hasBranch = false;
            bool hasComputedJump = false;
            for (size_t i = startIdx; i < limitIdx; ++i)
            {
                if (instructions[i].isReturn() || instructions[i].isJump())
                {
                    hasHardTerminator = true;
                    if (instructions[i].opcode == Opcode::JR && instructions[i].rs != Registers::RA)
                    {
                        hasComputedJump = true;
                    }
                }
                if (instructions[i].isBranch())
                {
                    hasBranch = true;
                }
            }

            // Check 2: cross-branch — a branch in funcA targets code in funcB.
            bool hasCrossBranch = false;
            if (hasBranch)
            {
                const Address nextNextFuncStart = (si + 2 < tempStarts.size())
                                                      ? tempStarts[si + 2]
                                                      : instructions.back().address + 4;

                for (size_t i = startIdx; i < limitIdx; ++i)
                {
                    if (instructions[i].isBranch())
                    {
                        if (auto target = instructions[i].getBranchTarget())
                        {
                            if (*target >= nextFuncStart && *target < nextNextFuncStart)
                            {
                                hasCrossBranch = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (!hasHardTerminator || hasCrossBranch)
            {
                // Do not merge if the next function start is a pinned
                // address (e.g. an explicitly-provided additionalStart),
                // unless a branch in the current range clearly targets the
                // next range. In that case, keeping the split would break a
                // single function into disconnected chunks.
                if (pinnedAddresses && pinnedAddresses->count(nextFuncStart))
                {
                    const bool isDirectCallTarget = directCallTargets.count(nextFuncStart) != 0;
                    const bool nextHasPrologue = detail::hasProloguePattern(instructions, limitIdx);
                    if (isDirectCallTarget || nextHasPrologue || !hasCrossBranch ||
                        !hasComputedJump)
                    {
                        continue;
                    }
                }
                startAddresses.erase(nextFuncStart);
                tempStarts.erase(tempStarts.begin() + static_cast<std::ptrdiff_t>(si + 1));
                merged = true;
                break; // restart outer loop
            }
        }
    }
}

std::vector<FunctionBoundary>
buildFunctionBoundaries(const std::vector<Instruction>& instructions,
                        const InstructionIndexMap& indexMap,
                        const std::unordered_set<Address>& startAddresses)
{
    std::vector<Address> sortedStarts(startAddresses.begin(), startAddresses.end());
    std::sort(sortedStarts.begin(), sortedStarts.end());

    std::vector<FunctionBoundary> boundaries;
    for (size_t startIndex = 0; startIndex < sortedStarts.size(); ++startIndex)
    {
        const Address start = sortedStarts[startIndex];
        auto it = indexMap.find(start);
        if (it == indexMap.end())
        {
            continue;
        }

        const size_t startInstructionIndex = it->second;
        const size_t limitIndex =
            (startIndex + 1 < sortedStarts.size() && indexMap.count(sortedStarts[startIndex + 1]))
                ? indexMap.at(sortedStarts[startIndex + 1])
                : instructions.size();

        Address end = instructions[limitIndex - 1].address;
        bool hasEpilogue = false;

        // Collect all branch targets within this function's range so that
        // we find the LAST return that's needed (not just the first).
        Address maxBranchTarget = start;
        for (size_t i = startInstructionIndex; i < limitIndex; ++i)
        {
            if (instructions[i].isBranch())
            {
                if (auto target = instructions[i].getBranchTarget())
                {
                    if (*target >= start && *target < instructions[limitIndex - 1].address + 4)
                    {
                        if (*target > maxBranchTarget)
                        {
                            maxBranchTarget = *target;
                        }
                    }
                }
            }
        }

        // Find the last return that covers all reachable code.
        for (size_t i = startInstructionIndex; i < limitIndex; ++i)
        {
            if (instructions[i].isReturn())
            {
                size_t endIndex = i;
                if (instructions[i].hasDelaySlot() && i + 1 < instructions.size())
                {
                    endIndex = i + 1;
                }
                end = instructions[endIndex].address;
                hasEpilogue = detail::hasEpiloguePattern(instructions, i);
                // Keep scanning if there are branch targets past this return.
                if (instructions[i].address >= maxBranchTarget)
                {
                    break;
                }
            }
        }

        // If branches within the function target code past the last return
        // but before the next function (out-of-line blocks like height
        // clamps that branch back), extend the function end to cover them.
        if (end < maxBranchTarget && maxBranchTarget < instructions[limitIndex - 1].address + 4)
        {
            end = instructions[limitIndex - 1].address;
        }

        boundaries.push_back({start, end,
                              detail::hasProloguePattern(instructions, startInstructionIndex),
                              hasEpilogue});
    }

    return boundaries;
}

} // namespace

std::vector<FunctionBoundary> findFunctionBoundaries(const std::vector<Instruction>& instructions)
{
    if (instructions.empty())
    {
        return {};
    }

    // Build instruction index early so we can validate JAL targets against
    // the actual instruction stream (important when code/data segmentation
    // excludes some addresses).
    auto indexMap = detail::buildInstructionIndex(instructions);
    auto startAddresses = collectFunctionStartAddresses(instructions, indexMap, nullptr);

    // ── Merge consecutive functions where the first falls through (no
    //    terminator) or has a branch that crosses into the next function.
    //
    //    Fall-through: PSn00bSDK wrapper stubs commonly set up arguments
    //    then fall through to the next function (no JR/J/branch between
    //    the two).  The prologue heuristic incorrectly splits them.
    //
    //    Cross-branch: a BEQ/BNE/etc. within function A targets code in
    //    function B (the next sequential function), meaning A and B are
    //    really one function that was incorrectly split.
    //
    //    In both cases we remove the spurious start address to merge.
    mergeConsecutiveFunctionSplits(instructions, indexMap, startAddresses);
    return buildFunctionBoundaries(instructions, indexMap, startAddresses);
}

std::vector<FunctionBoundary> findFunctionBoundaries(const std::vector<Instruction>& instructions,
                                                     const std::vector<Address>& additionalStarts)
{
    if (instructions.empty())
    {
        return {};
    }

    // Build instruction index early so we can validate JAL targets against
    // the actual instruction stream (important when code/data segmentation
    // excludes some addresses).
    auto indexMap = detail::buildInstructionIndex(instructions);
    auto startAddresses = collectFunctionStartAddresses(instructions, indexMap, &additionalStarts);

    // Build a set of pinned addresses from the additional starts.  These
    // are explicitly-identified entry points (from pointer harvesting or
    // the EXE entry point) that must survive the merge pass — even if the
    // preceding function appears to fall through into them.
    std::unordered_set<Address> pinned(additionalStarts.begin(), additionalStarts.end());

    // ── Merge consecutive functions where the first falls through or has
    //    a cross-branch (same unified logic as the single-arg overload).
    mergeConsecutiveFunctionSplits(instructions, indexMap, startAddresses, &pinned);
    return buildFunctionBoundaries(instructions, indexMap, startAddresses);
}

} // namespace disasm
} // namespace psxrecomp
