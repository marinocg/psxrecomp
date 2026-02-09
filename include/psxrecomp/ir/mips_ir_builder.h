#pragma once

#include "psxrecomp/disasm/instruction.h"
#include "psxrecomp/ir/ir.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace ir
{

/**
 * @brief Options for building IR from MIPS instructions.
 */
struct MipsIrBuildOptions
{
    bool emitUnknownAsNop = true;
};

/**
 * @brief Result of translating MIPS instructions into IR.
 */
struct MipsIrBuildResult
{
    std::vector<Instruction> instructions;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

/**
 * @brief Translate decoded MIPS instructions into linear IR.
 * @param instructions Disassembled MIPS instructions.
 * @param options Build options.
 * @return Translation result.
 */
MipsIrBuildResult buildIrFromMips(const std::vector<disasm::Instruction>& instructions,
                                  const MipsIrBuildOptions& options = {});

} // namespace ir
} // namespace psxrecomp
