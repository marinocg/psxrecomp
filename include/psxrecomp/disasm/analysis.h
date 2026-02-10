#pragma once

#include "psxrecomp/disasm/instruction.h"

#include <optional>
#include <vector>

namespace psxrecomp
{
namespace disasm
{

struct AddressRange
{
    Address start;
    Address end;
};

struct FunctionBoundary
{
    Address start;
    Address end;
    bool hasPrologue;
    bool hasEpilogue;
};

struct IndirectBranchTarget
{
    Address address;
    Register targetRegister;
    bool isCall;
};

struct JumpTableInfo
{
    Address jumpAddress;
    Register jumpRegister;
    Register tableRegister;
    std::optional<Register> indexRegister;
    std::optional<Address> tableBaseAddress;
    s16 tableOffset;
};

struct CodeDataSegmentation
{
    std::vector<AddressRange> codeRanges;
    std::vector<AddressRange> dataRanges;
};

struct CallGraphEdge
{
    Address caller;
    Address callSite;
    Address callee;
};

struct CallGraph
{
    std::vector<Address> functions;
    std::vector<CallGraphEdge> edges;
};

std::vector<FunctionBoundary> findFunctionBoundaries(const std::vector<Instruction>& instructions);

std::vector<IndirectBranchTarget>
findIndirectBranchTargets(const std::vector<Instruction>& instructions);

std::vector<JumpTableInfo> findJumpTables(const std::vector<Instruction>& instructions);

CodeDataSegmentation segmentCodeAndData(const std::vector<Instruction>& instructions,
                                        const std::vector<Address>& entryPoints,
                                        const std::vector<JumpTableInfo>& jumpTables);

CallGraph buildCallGraph(const std::vector<Instruction>& instructions,
                         const std::vector<FunctionBoundary>& boundaries);

} // namespace disasm
} // namespace psxrecomp
