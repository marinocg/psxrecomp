#pragma once

#include "pipeline_analysis_helpers.h"

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

struct PointerHarvestResults
{
    std::vector<Address> harvestedPointers;
    std::vector<Address> jumpTableHarvestedPointers;
    std::vector<Address> codeHarvestedPointers;
    std::vector<Address> knownPointerTableWords;
};

PointerHarvestResults harvestFunctionPointerSeeds(
    const std::vector<disasm::Instruction>& disassembled, const iso::PsxExeImage& exeImage,
    const disasm::CodeDataSegmentation& segmentation, const std::vector<Address>& entrySeeds,
    const std::vector<disasm::JumpTableInfo>& jumpTables, Address baseAddress);

std::vector<PipelineIndirectCallSite>
collectIndirectCallSiteMetadata(const std::vector<disasm::Instruction>& codeInstructions,
                                const std::vector<disasm::FunctionBoundary>& boundaries);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
