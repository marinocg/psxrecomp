#include "pipeline_harvest_helpers.h"

#include "pipeline_harvest_internal.h"

#include <algorithm>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{

std::optional<Address>
resolveStaticPointerWordAddress(const std::vector<disasm::Instruction>& functionInstructions,
                                size_t loadInstructionIndex)
{
    const auto& loadInstruction = functionInstructions[loadInstructionIndex];
    const Register baseRegister = loadInstruction.rs;
    for (size_t lookBack = 1; lookBack <= 12 && lookBack <= loadInstructionIndex; ++lookBack)
    {
        const size_t candidateIndex = loadInstructionIndex - lookBack;
        const auto& candidate = functionInstructions[candidateIndex];
        if (candidate.opcode != disasm::Opcode::LUI || candidate.rt != baseRegister)
        {
            if (writesRegister(candidate, baseRegister))
            {
                break;
            }
            continue;
        }

        const u32 hiImm = candidate.immediate & 0xFFFFu;
        for (size_t lookAhead = 1;
             candidateIndex + lookAhead < loadInstructionIndex && lookAhead <= 6; ++lookAhead)
        {
            const auto& next = functionInstructions[candidateIndex + lookAhead];
            if (next.opcode == disasm::Opcode::ADDIU && next.rs == baseRegister &&
                next.rt == baseRegister)
            {
                const u32 lo = next.immediate & 0xFFFFu;
                const s32 signedLo =
                    (lo & 0x8000u) ? static_cast<s32>(lo | 0xFFFF0000u) : static_cast<s32>(lo);
                return static_cast<Address>((hiImm << 16) + static_cast<u32>(signedLo) +
                                            static_cast<s32>(loadInstruction.immediate));
            }
            if (next.opcode == disasm::Opcode::ORI && next.rs == baseRegister &&
                next.rt == baseRegister)
            {
                return static_cast<Address>((hiImm << 16) | (next.immediate & 0xFFFFu)) +
                       static_cast<s32>(loadInstruction.immediate);
            }
            if (writesRegister(next, baseRegister))
            {
                break;
            }
        }
    }

    return std::nullopt;
}

} // namespace

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
                    if (auto pointerWordAddress =
                            resolveStaticPointerWordAddress(functionInstructions, index - lookBack);
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
