#include "psxrecomp/disasm/instruction.h"
#include "psxrecomp/ir/ir.h"
#include "psxrecomp/ir/mips_ir_builder.h"

#include <algorithm>
#include <cassert>

int main()
{
    using namespace psxrecomp::Registers;
    using psxrecomp::disasm::Instruction;
    using psxrecomp::disasm::InstructionType;
    using psxrecomp::disasm::Opcode;

    auto makeDelayNop = [](psxrecomp::Address address, psxrecomp::Address owner)
    {
        Instruction nop{};
        nop.address = address;
        nop.opcode = Opcode::SLL;
        nop.type = InstructionType::R_TYPE;
        nop.rd = ZERO;
        nop.rt = ZERO;
        nop.shamt = 0;
        nop.isInDelaySlot = true;
        nop.delaySlotOwner = owner;
        return nop;
    };

    Instruction jal{};
    jal.address = 0x80010000;
    jal.opcode = Opcode::JAL;
    jal.type = InstructionType::J_TYPE;
    jal.target = 0x00002000;

    Instruction jr{};
    jr.address = 0x80010008;
    jr.opcode = Opcode::JR;
    jr.type = InstructionType::R_TYPE;
    jr.rs = T0;

    Instruction jalr{};
    jalr.address = 0x80010010;
    jalr.opcode = Opcode::JALR;
    jalr.type = InstructionType::R_TYPE;
    jalr.rs = T1;
    jalr.rd = S0;

    auto result = psxrecomp::ir::buildIrFromMips({jal, makeDelayNop(0x80010004, 0x80010000), jr,
                                                  makeDelayNop(0x8001000C, 0x80010008), jalr,
                                                  makeDelayNop(0x80010014, 0x80010010)});

    assert(result.errors.empty());
    assert(result.warnings.empty());

    const auto hasInstruction =
        [&](psxrecomp::ir::Opcode opcode, psxrecomp::ir::Value input, psxrecomp::ir::Value output)
    {
        return std::any_of(result.instructions.begin(), result.instructions.end(),
                           [&](const auto& instr)
                           {
                               if (instr.opcode != opcode)
                               {
                                   return false;
                               }
                               if (input.kind != psxrecomp::ir::ValueKind::INVALID)
                               {
                                   if (instr.inputs.empty() || !(instr.inputs.front() == input))
                                   {
                                       return false;
                                   }
                               }
                               if (output.kind != psxrecomp::ir::ValueKind::INVALID)
                               {
                                   if (instr.outputs.empty() || !(instr.outputs.front() == output))
                                   {
                                       return false;
                                   }
                               }
                               return true;
                           });
    };

    assert(hasInstruction(psxrecomp::ir::Opcode::MOVE,
                          psxrecomp::ir::Value::makeImmediate(0x80010008),
                          psxrecomp::ir::Value::makeRegister(RA)));
    assert(hasInstruction(psxrecomp::ir::Opcode::JUMP, psxrecomp::ir::Value::makeRegister(T0),
                          psxrecomp::ir::Value::invalid()));
    assert(hasInstruction(psxrecomp::ir::Opcode::MOVE,
                          psxrecomp::ir::Value::makeImmediate(0x80010018),
                          psxrecomp::ir::Value::makeRegister(S0)));
    assert(hasInstruction(psxrecomp::ir::Opcode::CALL, psxrecomp::ir::Value::makeRegister(T1),
                          psxrecomp::ir::Value::invalid()));

    return 0;
}
