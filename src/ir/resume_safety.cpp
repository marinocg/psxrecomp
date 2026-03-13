#include "psxrecomp/ir/resume_safety.h"

#include <sstream>
#include <unordered_map>

namespace psxrecomp
{
namespace ir
{

std::vector<ResumeSafetyDiagnostic> verifyResumeSafety(const Function& function)
{
    std::vector<ResumeSafetyDiagnostic> diagnostics;

    for (const auto& block : function.blocks)
    {
        // Map from temporary ID → the source address where it was defined.
        std::unordered_map<u32, Address> tempDefAddress;

        for (const auto& instruction : block.instructions)
        {
            if (instruction.opcode == Opcode::PHI)
            {
                continue;
            }

            // Determine the effective source address for this instruction.
            // Instructions without a source address inherit the most recent
            // source address (they belong to the same MIPS instruction group).
            const std::optional<Address> effectiveAddress = instruction.sourceAddress;

            // Check inputs: if any input temporary was defined at a
            // different source address, this is a resume-unsafe pattern.
            if (effectiveAddress.has_value())
            {
                for (const auto& input : instruction.inputs)
                {
                    if (input.kind != ValueKind::TEMPORARY)
                    {
                        continue;
                    }
                    auto defIt = tempDefAddress.find(input.temporaryId);
                    if (defIt == tempDefAddress.end())
                    {
                        // Defined in a predecessor block — will be stale
                        // after resume.  This is a broader issue (cross-block
                        // temporaries) but is out of scope for this check
                        // since it requires whole-function analysis.
                        continue;
                    }
                    if (defIt->second != *effectiveAddress)
                    {
                        ResumeSafetyDiagnostic diag;
                        diag.blockName = block.name;
                        diag.defSourceAddress = defIt->second;
                        diag.useSourceAddress = *effectiveAddress;
                        diag.temporaryId = input.temporaryId;

                        std::ostringstream os;
                        os << "resume-unsafe: temp" << input.temporaryId << " defined at 0x"
                           << std::hex << defIt->second << " used at 0x" << *effectiveAddress
                           << " in block '" << block.name << "'";
                        diag.message = os.str();
                        diagnostics.push_back(std::move(diag));
                    }
                }
            }

            // Record outputs: associate each defined temporary with the
            // current source address.
            for (const auto& output : instruction.outputs)
            {
                if (output.kind == ValueKind::TEMPORARY && effectiveAddress.has_value())
                {
                    tempDefAddress[output.temporaryId] = *effectiveAddress;
                }
            }
        }
    }

    return diagnostics;
}

} // namespace ir
} // namespace psxrecomp
