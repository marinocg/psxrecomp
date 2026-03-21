#include "codegen_runtime_helpers.h"

#include "codegen_runtime_helpers_internal.h"

namespace psxrecomp
{
namespace recompiler
{

void emitRuntimeSupportHelpers(CppEmitter& emitter)
{
    emitRuntimeAccessHelpers(emitter);
    emitRuntimeExecutionHelpers(emitter);
}

} // namespace recompiler
} // namespace psxrecomp
