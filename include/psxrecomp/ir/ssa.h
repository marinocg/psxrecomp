#pragma once

#include "psxrecomp/ir/ir.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace ir
{

/**
 * @brief Result of SSA conversion.
 */
struct SsaBuildResult
{
    std::vector<std::string> errors;
};

/**
 * @brief Convert a function into SSA form with phi nodes.
 */
SsaBuildResult convertToSSA(Function& function);

} // namespace ir
} // namespace psxrecomp
