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
constexpr Register kReturnAddress = detail::kReturnAddress;

struct CallGraphEdgeKey
{
    Address caller;
    Address callSite;
    Address callee;

    bool operator==(const CallGraphEdgeKey& other) const
    {
        return caller == other.caller && callSite == other.callSite && callee == other.callee;
    }
};

struct CallGraphEdgeKeyHash
{
    size_t operator()(const CallGraphEdgeKey& key) const
    {
        size_t hash = static_cast<size_t>(key.caller);
        hash ^= static_cast<size_t>(key.callSite) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= static_cast<size_t>(key.callee) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

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
                                    std::unordered_set<Address>& startAddresses)
{
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

            // Check 1: fall-through — no terminator in [funcStart, nextFuncStart).
            bool hasTerminator = false;
            for (size_t i = startIdx; i < limitIdx; ++i)
            {
                if (instructions[i].isReturn() || instructions[i].isJump() ||
                    instructions[i].isBranch())
                {
                    hasTerminator = true;
                    break;
                }
            }

            // Check 2: cross-branch — a branch in funcA targets code in funcB.
            bool hasCrossBranch = false;
            if (hasTerminator)
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

            if (!hasTerminator || hasCrossBranch)
            {
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

    // ── Merge consecutive functions where the first falls through or has
    //    a cross-branch (same unified logic as the single-arg overload).
    mergeConsecutiveFunctionSplits(instructions, indexMap, startAddresses);
    return buildFunctionBoundaries(instructions, indexMap, startAddresses);
}

std::vector<IndirectBranchTarget>
findIndirectBranchTargets(const std::vector<Instruction>& instructions)
{
    std::vector<IndirectBranchTarget> targets;
    targets.reserve(instructions.size());
    for (const auto& instruction : instructions)
    {
        if (instruction.opcode != Opcode::JR && instruction.opcode != Opcode::JALR)
        {
            continue;
        }

        if (instruction.rs == kReturnAddress)
        {
            continue;
        }

        targets.push_back(
            {instruction.address, instruction.rs, instruction.opcode == Opcode::JALR});
    }

    return targets;
}

std::vector<JumpTableInfo> findJumpTables(const std::vector<Instruction>& instructions)
{
    std::vector<JumpTableInfo> tables;
    if (instructions.empty())
    {
        return tables;
    }

    for (size_t i = 0; i < instructions.size(); ++i)
    {
        const Instruction& instruction = instructions[i];
        if (instruction.opcode != Opcode::JR && instruction.opcode != Opcode::JALR)
        {
            continue;
        }

        if (instruction.rs == kReturnAddress)
        {
            continue;
        }

        Register jumpRegister = instruction.rs;
        std::optional<size_t> loadIndex;
        for (size_t back = 1; back <= 4 && i >= back; ++back)
        {
            const Instruction& candidate = instructions[i - back];
            if (candidate.opcode == Opcode::LW && candidate.rt == jumpRegister)
            {
                loadIndex = i - back;
                break;
            }
        }

        if (!loadIndex)
        {
            continue;
        }

        const Instruction& loadInstruction = instructions[*loadIndex];
        Register tableRegister = loadInstruction.rs;
        s16 tableOffset = loadInstruction.immediate;

        std::optional<Register> indexRegister;
        for (size_t back = 1; back <= 4 && *loadIndex >= back; ++back)
        {
            const Instruction& candidate = instructions[*loadIndex - back];
            if ((candidate.opcode == Opcode::ADDU || candidate.opcode == Opcode::ADD) &&
                candidate.rd == tableRegister)
            {
                if (candidate.rs == tableRegister)
                {
                    indexRegister = candidate.rt;
                }
                else if (candidate.rt == tableRegister)
                {
                    indexRegister = candidate.rs;
                }
                break;
            }
        }

        auto tableBaseAddress =
            detail::resolveImmediateAddress(instructions, *loadIndex, tableRegister);

        tables.push_back({instruction.address, jumpRegister, tableRegister, indexRegister,
                          tableBaseAddress, tableOffset});
    }

    return tables;
}

CodeDataSegmentation segmentCodeAndData(const std::vector<Instruction>& instructions,
                                        const std::vector<Address>& entryPoints,
                                        const std::vector<JumpTableInfo>& jumpTables)
{
    CodeDataSegmentation segmentation;
    if (instructions.empty())
    {
        return segmentation;
    }

    auto indexMap = detail::buildInstructionIndex(instructions);
    std::unordered_set<Address> visited;
    std::queue<Address> worklist;

    auto visitIfValid = [&](Address address, bool enqueue)
    {
        if (indexMap.count(address) == 0)
        {
            return;
        }

        if (visited.insert(address).second && enqueue)
        {
            worklist.push(address);
        }
    };

    if (entryPoints.empty())
    {
        visitIfValid(instructions.front().address, true);
    }
    else
    {
        for (Address entry : entryPoints)
        {
            visitIfValid(entry, true);
        }
    }

    std::unordered_map<Address, JumpTableInfo> jumpTableMap;
    for (const auto& table : jumpTables)
    {
        jumpTableMap.emplace(table.jumpAddress, table);
    }

    while (!worklist.empty())
    {
        const Address address = worklist.front();
        worklist.pop();

        const size_t index = indexMap[address];
        const Instruction& instruction = instructions[index];

        const auto enqueueDelaySlot = [&](bool enqueue)
        {
            if (index + 1 < instructions.size())
            {
                visitIfValid(instructions[index + 1].address, enqueue);
            }
        };

        const auto enqueueAfterDelaySlot = [&]()
        {
            if (index + 2 < instructions.size())
            {
                visitIfValid(instructions[index + 2].address, true);
            }
        };

        if (instruction.isBranch())
        {
            if (auto target = instruction.getBranchTarget())
            {
                visitIfValid(*target, true);
            }

            enqueueDelaySlot(true);
            enqueueAfterDelaySlot();
            continue;
        }

        if (instruction.isJump())
        {
            if (instruction.hasDelaySlot())
            {
                const bool isCall =
                    instruction.opcode == Opcode::JAL || instruction.opcode == Opcode::JALR;
                enqueueDelaySlot(isCall);
            }

            if (instruction.opcode == Opcode::J || instruction.opcode == Opcode::JAL)
            {
                if (auto target = instruction.getJumpTarget())
                {
                    visitIfValid(*target, true);
                }
            }

            if (instruction.opcode == Opcode::JAL || instruction.opcode == Opcode::JALR)
            {
                enqueueAfterDelaySlot();
            }

            if (instruction.opcode == Opcode::JR && instruction.rs == kReturnAddress)
            {
                continue;
            }

            if (jumpTableMap.count(instruction.address) > 0)
            {
                continue;
            }

            continue;
        }

        if (index + 1 < instructions.size())
        {
            visitIfValid(instructions[index + 1].address, true);
        }
    }

    std::unordered_set<Address> codeAddresses = visited;
    std::unordered_set<Address> dataAddresses;
    dataAddresses.reserve(instructions.size());

    for (const auto& instruction : instructions)
    {
        if (codeAddresses.count(instruction.address) == 0)
        {
            dataAddresses.insert(instruction.address);
        }
    }

    segmentation.codeRanges = detail::buildRanges(instructions, codeAddresses);
    segmentation.dataRanges = detail::buildRanges(instructions, dataAddresses);

    return segmentation;
}

CallGraph buildCallGraph(const std::vector<Instruction>& instructions,
                         const std::vector<FunctionBoundary>& boundaries)
{
    CallGraph graph;
    if (instructions.empty() || boundaries.empty())
    {
        return graph;
    }

    std::vector<FunctionBoundary> sortedBoundaries = boundaries;
    std::sort(sortedBoundaries.begin(), sortedBoundaries.end(),
              [](const FunctionBoundary& lhs, const FunctionBoundary& rhs)
              { return lhs.start < rhs.start; });

    std::unordered_set<Address> knownFunctions;
    knownFunctions.reserve(sortedBoundaries.size());
    for (const auto& boundary : sortedBoundaries)
    {
        graph.functions.push_back(boundary.start);
        knownFunctions.insert(boundary.start);
    }

    std::unordered_set<CallGraphEdgeKey, CallGraphEdgeKeyHash> seenEdges;
    seenEdges.reserve(instructions.size());

    for (const auto& instruction : instructions)
    {
        if (!detail::isDirectCall(instruction))
        {
            continue;
        }

        const auto callee = detail::resolveDirectCallTarget(instruction);
        if (!callee.has_value())
        {
            continue;
        }

        const auto boundaryIt =
            std::upper_bound(sortedBoundaries.begin(), sortedBoundaries.end(), instruction.address,
                             [](Address address, const FunctionBoundary& boundary)
                             { return address < boundary.start; });
        if (boundaryIt == sortedBoundaries.begin())
        {
            continue;
        }

        const FunctionBoundary& callerBoundary = *std::prev(boundaryIt);
        if (instruction.address > callerBoundary.end)
        {
            continue;
        }

        const Address caller = callerBoundary.start;
        const CallGraphEdgeKey edgeKey{caller, instruction.address, *callee};
        if (seenEdges.insert(edgeKey).second)
        {
            graph.edges.push_back({caller, instruction.address, *callee});
        }
        if (knownFunctions.insert(*callee).second)
        {
            graph.functions.push_back(*callee);
        }
    }

    std::sort(graph.functions.begin(), graph.functions.end());
    std::sort(graph.edges.begin(), graph.edges.end(),
              [](const CallGraphEdge& lhs, const CallGraphEdge& rhs)
              {
                  if (lhs.caller != rhs.caller)
                  {
                      return lhs.caller < rhs.caller;
                  }
                  if (lhs.callSite != rhs.callSite)
                  {
                      return lhs.callSite < rhs.callSite;
                  }
                  return lhs.callee < rhs.callee;
              });

    return graph;
}

} // namespace disasm
} // namespace psxrecomp
