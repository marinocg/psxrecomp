#pragma once

#include "psxrecomp/ir/ir.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace ir
{

/**
 * @brief Diagnostic emitted when a resume-unsafe temporary usage is detected.
 *
 * A resume-unsafe usage occurs when an instruction at source address B
 * consumes a temporary defined at source address A (A != B), within the
 * same basic block.  When the block is entered via a mid-block resume at
 * address B, the defining instruction at A is skipped, leaving the
 * temporary at its default-initialized value (0).
 */
struct ResumeSafetyDiagnostic
{
    std::string blockName;
    Address defSourceAddress = 0;
    Address useSourceAddress = 0;
    u32 temporaryId = 0;
    std::string message;
};

/**
 * @brief Verify that a function's blocks do not contain resume-unsafe
 *        temporary usage patterns.
 *
 * For each basic block, checks that no instruction at source address B
 * uses a temporary that was defined at a different source address A
 * (both within the same block).  Such patterns are unsafe because
 * mid-block resume at B would skip the defining instruction at A.
 *
 * @return A list of diagnostics.  Empty means the function is resume-safe.
 */
std::vector<ResumeSafetyDiagnostic> verifyResumeSafety(const Function& function);

} // namespace ir
} // namespace psxrecomp
