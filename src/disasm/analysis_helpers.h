#pragma once

#include "psxrecomp/disasm/analysis.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace psxrecomp
{
namespace disasm
{
namespace detail
{

constexpr Register kStackPointer = Registers::SP;
constexpr Register kReturnAddress = Registers::RA;

bool hasProloguePattern(const std::vector<Instruction>& instructions, size_t index);
bool hasEpiloguePattern(const std::vector<Instruction>& instructions, size_t index);

std::unordered_map<Address, size_t>
buildInstructionIndex(const std::vector<Instruction>& instructions);

std::optional<Address> resolveImmediateAddress(const std::vector<Instruction>& instructions,
                                               size_t index, Register reg);

std::vector<AddressRange> buildRanges(const std::vector<Instruction>& instructions,
                                      const std::unordered_set<Address>& addresses);

bool isDirectCall(const Instruction& instruction);
std::optional<Address> resolveDirectCallTarget(const Instruction& instruction);

} // namespace detail
} // namespace disasm
} // namespace psxrecomp
