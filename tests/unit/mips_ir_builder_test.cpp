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

psxrecomp::u32 encodeJ(psxrecomp::u32 opcode, psxrecomp::u32 target)
{
    return (opcode << 26) | (target & 0x03FFFFFFu);
}

psxrecomp::u32 encodeCop0(psxrecomp::u32 rs, psxrecomp::u32 rt, psxrecomp::u32 rd,
                          psxrecomp::u32 funct = 0)
{
    return (0x10u << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (funct & 0x3Fu);
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
    assert(result.instructions.size() == 9);
    assert(result.instructions[0].opcode == Opcode::ADD);
    assert(result.instructions[1].opcode == Opcode::ADD);
    assert(result.instructions[2].opcode == Opcode::COMPARE_EQ);
    assert(result.instructions[3].opcode == Opcode::NOP);
    assert(result.instructions[4].opcode == Opcode::BRANCH);
    assert(result.instructions[5].opcode == Opcode::ADD);
    assert(result.instructions[6].opcode == Opcode::LOAD);
    assert(result.instructions[7].opcode == Opcode::ADD);
    assert(result.instructions[8].opcode == Opcode::STORE);
    assert(result.instructions[0].sourceAsm.has_value());
    assert(result.instructions[2].sourceAsm.has_value());
    assert(result.instructions[0].sourceAsm.value() == instructions[0].toString());
    assert(result.instructions[2].sourceAsm.value() == instructions[2].toString());

    const Address branchAddress = result.instructions[2].sourceAddress.value_or(0);
    assert(branchAddress == 0x80010008);

    std::vector<psxrecomp::u8> extendedBuffer;
    extendedBuffer.reserve(44);
    appendLe32(extendedBuffer, encodeR(9, 10, 11, 0, 0x27)); // nor $t3, $t1, $t2
    appendLe32(extendedBuffer, encodeI(0x0A, 11, 12, 5));    // slti $t4, $t3, 5
    appendLe32(extendedBuffer, encodeI(0x0B, 11, 14, 5));    // sltiu $t6, $t3, 5
    appendLe32(extendedBuffer, encodeI(0x20, 8, 13, 1));     // lb $t5, 1($t0)
    appendLe32(extendedBuffer, encodeI(0x28, 8, 13, 2));     // sb $t5, 2($t0)
    appendLe32(extendedBuffer, encodeJ(0x03, 0x00000028u));  // jal 0xA0 (BIOS)
    appendLe32(extendedBuffer, encodeR(0, 0, 0, 0, 0x00));   // nop delay slot
    appendLe32(extendedBuffer, encodeI(0x01, 4, 17, 1));     // bgezal $a0, +1
    appendLe32(extendedBuffer, encodeR(0, 0, 0, 0, 0x00));   // nop delay slot

    auto extendedInstructions =
        MipsDisassembler::disassemble(extendedBuffer.data(), extendedBuffer.size(), 0x80020000);
    auto extended = buildIrFromMips(extendedInstructions);

    assert(extended.errors.empty());
    bool foundNorXor = false;
    bool foundSlti = false;
    bool foundSltu = false;
    bool foundLoadStore = false;
    bool foundBiosSyscall = false;
    bool foundConditionalLink = false;
    for (const auto& instruction : extended.instructions)
    {
        if (instruction.opcode == Opcode::XOR)
        {
            foundNorXor = true;
        }
        if (instruction.opcode == Opcode::COMPARE_LT && !instruction.outputs.empty() &&
            instruction.outputs.front().kind == psxrecomp::ir::ValueKind::REGISTER)
        {
            foundSlti = true;
        }
        if (instruction.opcode == Opcode::COMPARE_LTU)
        {
            foundSltu = true;
        }
        if (instruction.opcode == Opcode::LOAD || instruction.opcode == Opcode::STORE ||
            instruction.opcode == Opcode::LOAD8 || instruction.opcode == Opcode::LOAD8U ||
            instruction.opcode == Opcode::LOAD16 || instruction.opcode == Opcode::LOAD16U ||
            instruction.opcode == Opcode::STORE8 || instruction.opcode == Opcode::STORE16)
        {
            foundLoadStore = true;
        }
        if (instruction.opcode == Opcode::CALL && !instruction.inputs.empty() &&
            instruction.inputs.front().kind == psxrecomp::ir::ValueKind::ADDRESS &&
            (instruction.inputs.front().address & 0x1FFFFFFFu) == 0xA0u)
        {
            foundBiosSyscall = true;
        }
        if (instruction.opcode == Opcode::BRANCH && !instruction.inputs.empty())
        {
            foundConditionalLink = true;
        }
    }

    assert(foundNorXor);
    assert(foundSlti);
    assert(foundLoadStore);
    assert(foundSltu);
    assert(foundBiosSyscall);
    assert(foundConditionalLink);

    std::vector<psxrecomp::u8> cop0Buffer;
    cop0Buffer.reserve(16);
    appendLe32(cop0Buffer, encodeCop0(0x04, 2, 12));      // mtc0 $v0, $c12
    appendLe32(cop0Buffer, encodeCop0(0x00, 3, 12));      // mfc0 $v1, $c12
    appendLe32(cop0Buffer, encodeCop0(0x10, 0, 0, 0x10)); // rfe
    appendLe32(cop0Buffer, encodeR(0, 0, 0, 0, 0x00));    // nop

    auto cop0Instructions =
        MipsDisassembler::disassemble(cop0Buffer.data(), cop0Buffer.size(), 0x80030000);
    auto cop0Result = buildIrFromMips(cop0Instructions);

    assert(cop0Result.errors.empty());
    bool foundCop0Mtc = false;
    bool foundCop0Mfc = false;
    bool foundCop0Rfe = false;
    for (const auto& instruction : cop0Result.instructions)
    {
        if (instruction.opcode == Opcode::COP0_MTC && instruction.inputs.size() == 2 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == 12)
        {
            foundCop0Mtc = true;
        }
        if (instruction.opcode == Opcode::COP0_MFC && instruction.inputs.size() == 1 &&
            instruction.outputs.size() == 1 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == 12)
        {
            foundCop0Mfc = true;
        }
        if (instruction.opcode == Opcode::COP0_RFE)
        {
            foundCop0Rfe = true;
        }
    }

    assert(foundCop0Mtc);
    assert(foundCop0Mfc);
    assert(foundCop0Rfe);

    return 0;
}
