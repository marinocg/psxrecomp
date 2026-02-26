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
} // namespace

std::vector<IndirectBranchTarget> findIndirectBranchTargets(const std::vector<Instruction>& instructions)
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
