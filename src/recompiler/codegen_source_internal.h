#pragma once

#include "cpp_emitter.h"
#include "psxrecomp/recompiler/codegen.h"

#include <string>
#include <utility>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{

struct FunctionDispatchRange
{
    Address start = 0;
    Address endExclusive = 0;
    std::string functionSymbol;
};

void emitGeneratedSourceBody(CppEmitter& emitter, const ModuleMetadata& metadata,
                             Address moduleEntryAddress,
                             const std::vector<std::pair<Address, std::string>>& functionSymbols);

} // namespace recompiler
} // namespace psxrecomp
