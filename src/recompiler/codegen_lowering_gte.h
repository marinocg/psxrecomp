#pragma once

#include "codegen_lowering_helpers.h"

#include <string>
#include <unordered_map>

namespace psxrecomp
{
namespace recompiler
{

bool emitGteInstruction(const ir::Instruction& instruction, const ir::BasicBlock& block,
                        const std::unordered_map<std::string, std::string>& blockNames,
                        LoweringContext& context, CppEmitter& emitter);

} // namespace recompiler
} // namespace psxrecomp
