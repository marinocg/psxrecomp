#include "psxrecomp/disasm/instruction.h"
#include "psxrecomp/ir/mips_ir_builder.h"

#include <cassert>
#include <vector>

namespace
{
psxrecomp::u32 encodeR(psxrecomp::u32 rs, psxrecomp::u32 rt, psxrecomp::u32 rd,
                       psxrecomp::u32 shamt, psxrecomp::u32 funct)
{
    return (rs << 21) | (rt << 16) | (rd << 11) | (shamt << 6) | funct;
}

psxrecomp::u32 encodeI(psxrecomp::u32 opcode, psxrecomp::u32 rs, psxrecomp::u32 rt,
                       psxrecomp::s16 imm)
{
    return (opcode << 26) | (rs << 21) | (rt << 16) | static_cast<psxrecomp::u16>(imm);
}

void appendLe32(std::vector<psxrecomp::u8>& buffer, psxrecomp::u32 value)
{
    buffer.push_back(static_cast<psxrecomp::u8>(value & 0xFF));
    buffer.push_back(static_cast<psxrecomp::u8>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<psxrecomp::u8>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<psxrecomp::u8>((value >> 24) & 0xFF));
}
} // namespace

int main()
{
    using psxrecomp::Address;
    using psxrecomp::disasm::MipsDisassembler;
    using psxrecomp::ir::buildIrFromMips;
    using psxrecomp::ir::Opcode;

    std::vector<psxrecomp::u8> buffer;
    buffer.reserve(24);

    appendLe32(buffer, encodeI(0x09, 0, 8, 4));    // addiu $t0, $zero, 4
    appendLe32(buffer, encodeR(8, 8, 9, 0, 0x20)); // add $t1, $t0, $t0
    appendLe32(buffer, encodeI(0x04, 9, 0, 1));    // beq $t1, $zero, +1
    appendLe32(buffer, encodeR(0, 0, 0, 0, 0x00)); // nop (delay slot)
    appendLe32(buffer, encodeI(0x23, 9, 10, 0));   // lw $t2, 0($t1)
    appendLe32(buffer, encodeI(0x2B, 9, 10, 4));   // sw $t2, 4($t1)

    auto instructions = MipsDisassembler::disassemble(buffer.data(), buffer.size(), 0x80010000);
    auto result = buildIrFromMips(instructions);

    assert(result.errors.empty());
    assert(result.instructions.size() == 8);
    assert(result.instructions[0].opcode == Opcode::ADD);
    assert(result.instructions[1].opcode == Opcode::ADD);
    assert(result.instructions[2].opcode == Opcode::COMPARE_EQ);
    assert(result.instructions[3].opcode == Opcode::BRANCH);
    assert(result.instructions[4].opcode == Opcode::ADD);
    assert(result.instructions[5].opcode == Opcode::LOAD);
    assert(result.instructions[6].opcode == Opcode::ADD);
    assert(result.instructions[7].opcode == Opcode::STORE);

    const Address branchAddress = result.instructions[2].sourceAddress.value_or(0);
    assert(branchAddress == 0x80010008);

    return 0;
}
