#pragma once

#include "psxrecomp/ir/ir.h"

#include <sstream>
#include <string>

namespace psxrecomp
{
namespace recompiler
{

static inline bool instructionIsInDelaySlot(const ir::Instruction& instruction)
{
    return instruction.sourceAsmAddress.has_value() && instruction.sourceAddress.has_value() &&
           instruction.sourceAsmAddress.value() != instruction.sourceAddress.value();
}

static inline std::string instructionSourcePcExpr(const ir::Instruction& instruction)
{
    if (!instruction.sourceAddress.has_value())
    {
        return "0";
    }

    std::ostringstream stream;
    stream << "0x" << std::hex << instruction.sourceAddress.value();
    return stream.str();
}

} // namespace recompiler
} // namespace psxrecomp
