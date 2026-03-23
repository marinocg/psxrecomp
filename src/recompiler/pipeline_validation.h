#pragma once

#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/pipeline.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

std::optional<std::string> verifyFunctionForCodegen(const ir::Function& function,
                                                    std::string_view activeDiscPath,
                                                    std::string_view stage,
                                                    std::vector<PipelineDiagnostic>& diagnostics);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp