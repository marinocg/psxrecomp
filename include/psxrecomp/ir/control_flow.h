#pragma once

#include "psxrecomp/ir/ir.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace psxrecomp
{
namespace ir
{

/**
 * @brief Result from control-flow construction.
 */
struct ControlFlowBuildResult
{
    Function function;
    std::unordered_map<Address, std::string> addressToBlockName;
    std::vector<std::string> errors;
};

/**
 * @brief Build basic blocks and CFG edges from a linear instruction stream.
 */
ControlFlowBuildResult buildControlFlowFunction(std::string_view functionName, Address entryAddress,
                                                const std::vector<Instruction>& instructions);

} // namespace ir
} // namespace psxrecomp
