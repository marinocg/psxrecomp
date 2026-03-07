#pragma once

#include "cpp_emitter.h"

#include <string>

namespace psxrecomp
{
namespace recompiler
{

void emitRunnerSupportCommon(CppEmitter& emitter);
void emitRunnerSupportPresenter(CppEmitter& emitter);
void emitRunnerMainFunction(CppEmitter& emitter, const std::string& moduleName);
void emitRunnerExceptionBlock(CppEmitter& emitter);

} // namespace recompiler
} // namespace psxrecomp
