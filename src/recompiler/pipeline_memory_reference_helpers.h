#pragma once

#include "pipeline_analysis_helpers.h"

#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

bool isWordMemoryReferenceOpcode(disasm::Opcode opcode);
bool isKnownIndirectPointerWord(const std::unordered_set<Address>& knownWords, Address address);

std::optional<Address>
resolveStaticMemoryReferenceAddress(const std::vector<disasm::Instruction>& instructions,
                                    size_t memoryInstructionIndex, Address moduleBase = 0,
                                    Address moduleEnd = 0);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
