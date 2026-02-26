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

struct PipelineCodeLayout
{
    std::vector<disasm::Instruction> codeInstructions;
    std::vector<disasm::FunctionBoundary> boundaries;
    disasm::CodeDataSegmentation segmentation;
};

PipelineCodeLayout analyzeCodeLayout(const std::vector<disasm::Instruction>& disassembled,
                                     const iso::PsxExeImage& exeImage, Address baseAddress,
                                     Address entryAddress);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
