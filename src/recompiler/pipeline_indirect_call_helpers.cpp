#include "pipeline_harvest_helpers.h"

#include "pipeline_harvest_internal.h"
#include "pipeline_memory_reference_helpers.h"

#include <algorithm>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

std::vector<PipelineIndirectCallSite>
collectIndirectCallSiteMetadata(const std::vector<disasm::Instruction>& codeInstructions,
                                const std::vector<disasm::FunctionBoundary>& boundaries)
{
    std::vector<PipelineIndirectCallSite> sites;
    for (const auto& boundary : boundaries)
    {
        auto rangeBegin =
            std::lower_bound(codeInstructions.begin(), codeInstructions.end(), boundary.start,
                             [](const disasm::Instruction& instruction, Address target)
                             { return instruction.address < target; });
        auto rangeEnd = std::upper_bound(rangeBegin, codeInstructions.end(), boundary.end,
                                         [](Address target, const disasm::Instruction& instruction)
                                         { return target < instruction.address; });

        std::vector<disasm::Instruction> functionInstructions(rangeBegin, rangeEnd);
        for (size_t index = 0; index < functionInstructions.size(); ++index)
        {
            const auto& instruction = functionInstructions[index];
            if (instruction.opcode != disasm::Opcode::JALR)
            {
                continue;
            }

            PipelineIndirectCallSite site;
            site.callerPc = instruction.address;
            site.containingFunction = boundary.start;
            site.sourceRegister = instruction.rs;
            for (size_t lookBack = 1; lookBack <= 8 && lookBack <= index; ++lookBack)
            {
                const auto& previous = functionInstructions[index - lookBack];
                if (previous.opcode == disasm::Opcode::LW && previous.rt == instruction.rs)
                {
                    site.pointerBaseRegister = static_cast<s8>(previous.rs);
                    site.pointerOffset = static_cast<s16>(previous.immediate);
                    site.pointerLoadClobbersBase = previous.rs == previous.rt;
                    if (auto pointerWordAddress = resolveStaticMemoryReferenceAddress(
                            functionInstructions, index - lookBack);
                        pointerWordAddress.has_value())
                    {
                        site.pointerWordAddress = *pointerWordAddress;
                        site.hasStaticPointerWordAddress = true;
                    }
                    break;
                }
                if (writesRegister(previous, instruction.rs))
                {
                    break;
                }
            }
            sites.push_back(site);
        }
    }

    std::sort(sites.begin(), sites.end(),
              [](const PipelineIndirectCallSite& lhs, const PipelineIndirectCallSite& rhs)
              { return lhs.callerPc < rhs.callerPc; });
    return sites;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
