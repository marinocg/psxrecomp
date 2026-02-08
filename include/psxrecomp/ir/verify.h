#pragma once

#include "psxrecomp/ir/control_flow.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace ir
{

struct VerificationResult
{
    bool ok = true;
    std::vector<std::string> errors;
};

VerificationResult verifyFunction(const Function& function, const ControlFlowGraph& graph);
VerificationResult verifyProgram(const Program& program);

} // namespace ir
} // namespace psxrecomp
