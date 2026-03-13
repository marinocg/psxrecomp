#pragma once

#include "psxrecomp/disasm/analysis.h"
#include "psxrecomp/disasm/instruction.h"
#include "psxrecomp/iso/psx_exe_loader.h"
#include "psxrecomp/types.h"

#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

struct PipelineIndirectCallSite
{
    Address callerPc = 0;
    Address containingFunction = 0;
    Address pointerWordAddress = 0;
    u8 sourceRegister = 0xFF;
    s8 pointerBaseRegister = -1;
    s16 pointerOffset = 0;
    bool hasStaticPointerWordAddress = false;
    bool pointerLoadClobbersBase = false;
};

struct PipelineCodeLayout
{
    std::vector<disasm::Instruction> codeInstructions;
    std::vector<disasm::FunctionBoundary> boundaries;
    disasm::CodeDataSegmentation segmentation;
    std::vector<Address> harvestedFunctionEntries;
    std::vector<Address> knownPointerTableWords;
    std::vector<PipelineIndirectCallSite> indirectCallSites;
};

PipelineCodeLayout analyzeCodeLayout(const std::vector<disasm::Instruction>& disassembled,
                                     const iso::PsxExeImage& exeImage, Address baseAddress,
                                     Address entryAddress);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
