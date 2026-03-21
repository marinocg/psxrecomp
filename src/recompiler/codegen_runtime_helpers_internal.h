#pragma once

namespace psxrecomp
{
namespace recompiler
{

class CppEmitter;

void emitRuntimeAccessHelpers(CppEmitter& emitter);
void emitRuntimeExecutionHelpers(CppEmitter& emitter);

} // namespace recompiler
} // namespace psxrecomp