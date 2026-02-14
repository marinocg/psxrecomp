#include "psxrecomp/disasm/instruction.h"
#include "psxrecomp/ir/mips_ir_builder.h"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    using psxrecomp::disasm::Instruction;
    using psxrecomp::disasm::InstructionType;
    using psxrecomp::disasm::Opcode;
    using psxrecomp::ir::MipsIrBuildOptions;

    Instruction unsupported{};
    unsupported.address = 0x80013C28;
    unsupported.encoding = 0x80820000;
    unsupported.opcode = Opcode::UNKNOWN;
    unsupported.type = InstructionType::UNKNOWN;

    Instruction jumpWithDelay{};
    jumpWithDelay.address = 0x80013C64;
    jumpWithDelay.encoding = 0x08000000;
    jumpWithDelay.opcode = Opcode::J;
    jumpWithDelay.type = InstructionType::J_TYPE;

    Instruction unsupportedInDelay{};
    unsupportedInDelay.address = 0x80013C68;
    unsupportedInDelay.encoding = 0x8043FFFF;
    unsupportedInDelay.opcode = Opcode::UNKNOWN;
    unsupportedInDelay.type = InstructionType::UNKNOWN;
    unsupportedInDelay.isInDelaySlot = true;
    unsupportedInDelay.delaySlotOwner = 0x80013C64;

    Instruction breakInstr{};
    breakInstr.address = 0x80014044;
    breakInstr.encoding = 0x0007000D;
    breakInstr.opcode = Opcode::BREAK;
    breakInstr.type = InstructionType::R_TYPE;

    MipsIrBuildOptions options;
    options.emitUnknownAsNop = true;

    auto result = psxrecomp::ir::buildIrFromMips(
        {unsupported, breakInstr, jumpWithDelay, unsupportedInDelay}, options);

    assert(result.warnings.size() == 3);
    assert(result.warnings[0].find("Unsupported opcode:") != std::string::npos);
    assert(result.warnings[0].find("word=0x80820000") != std::string::npos);
    assert(result.warnings[0].find("op=0x20") != std::string::npos);
    assert(result.warnings[0].find("@ 0x80013c28") != std::string::npos);

    assert(result.warnings[2].find("word=0x8043ffff") != std::string::npos);
    assert(result.warnings[2].find("in_delay_slot") != std::string::npos);
    assert(result.warnings[2].find("owner=0x80013c64") != std::string::npos);

    assert(result.warnings[1].find("BREAK lowered to TRAP") != std::string::npos);
    assert(result.warnings[1].find("@ 0x80014044") != std::string::npos);

    return 0;
}
