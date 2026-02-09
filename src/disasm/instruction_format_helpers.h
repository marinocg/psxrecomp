#pragma once

#include "psxrecomp/disasm/instruction.h"

#include <functional>
#include <optional>
#include <string>

namespace psxrecomp
{
namespace disasm
{
namespace detail
{

std::string formatHex(u32 value, int width);
std::string formatImmediateSigned(s16 value);
std::string formatImmediateUnsigned(u16 value);
std::string formatCopRegister(u8 reg);
std::string formatGteDataRegister(u8 reg);
std::string formatGteControlRegister(u8 reg);
std::string formatGteCommand(Opcode opcode);
u32 extractSpecialCode(u32 encoding);
std::string
resolveTargetString(const Instruction& instruction,
                    const std::function<std::optional<std::string>(Address)>& labelResolver);

} // namespace detail
} // namespace disasm
} // namespace psxrecomp
