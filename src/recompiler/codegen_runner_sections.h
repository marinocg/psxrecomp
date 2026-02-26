#pragma once

#include "cpp_emitter.h"

#include <string>

namespace psxrecomp
{
namespace recompiler
{

void emitRunnerMainFunction(CppEmitter& emitter, const std::string& moduleName);

} // namespace recompiler
} // namespace psxrecomp
