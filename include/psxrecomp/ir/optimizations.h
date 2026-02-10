#pragma once

#include "psxrecomp/ir/ir.h"

#include <cstddef>

namespace psxrecomp
{
namespace ir
{

struct OptimizationStats
{
    size_t constantsFolded = 0;
    size_t deadInstructionsRemoved = 0;
    size_t cseReplacements = 0;
    size_t licmMoved = 0;
};

OptimizationStats runOptimizations(Function& function);
OptimizationStats runOptimizations(Program& program);

} // namespace ir
} // namespace psxrecomp
