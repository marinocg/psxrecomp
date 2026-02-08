#pragma once

#include "psxrecomp/ir/control_flow.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace ir
{

struct SsaResult
{
    bool changed = false;
    std::vector<std::string> warnings;
};

SsaResult convertFunctionToSSA(Function& function, const ControlFlowGraph& graph);

} // namespace ir
} // namespace psxrecomp
