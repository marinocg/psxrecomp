#include "psxrecomp/disasm/instruction.h"
#include "psxrecomp/ir/mips_ir_builder.h"
#include "psxrecomp/runtime/cop0.h"

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

psxrecomp::u32 encodeCop2Transfer(psxrecomp::u32 rs, psxrecomp::u32 rt, psxrecomp::u32 rd)
{
    return (0x12u << 26) | (rs << 21) | (rt << 16) | (rd << 11);
}

psxrecomp::u32 encodeGteCommand(psxrecomp::u32 rawCommandBits)
{
    return 0x4A000000u | (rawCommandBits & 0x01FFFFFFu);
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
    assert(result.instructions[3].sourceAddress.value_or(0) == 0x80010008);
    assert(result.instructions[3].sourceAsmAddress.value_or(0) == 0x8001000C);

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
    cop0Buffer.reserve(24);
    appendLe32(cop0Buffer, encodeCop0(0x04, 2, 12));      // mtc0 $v0, $c12
    appendLe32(cop0Buffer, encodeCop0(0x00, 3, 12));      // mfc0 $v1, $c12
    appendLe32(cop0Buffer, encodeCop0(0x06, 4, 12));      // ctc0 $a0, $c12
    appendLe32(cop0Buffer, encodeCop0(0x02, 5, 12));      // cfc0 $a1, $c12
    appendLe32(cop0Buffer, encodeCop0(0x10, 0, 0, 0x10)); // rfe
    appendLe32(cop0Buffer, encodeR(0, 0, 0, 0, 0x00));    // nop

    auto cop0Instructions =
        MipsDisassembler::disassemble(cop0Buffer.data(), cop0Buffer.size(), 0x80030000);
    auto cop0Result = buildIrFromMips(cop0Instructions);

    assert(cop0Result.errors.empty());
    bool foundCop0Mtc = false;
    bool foundCop0Mfc = false;
    bool foundCtc0Alias = false;
    bool foundCfc0Alias = false;
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
        if (instruction.opcode == Opcode::COP0_MTC && instruction.inputs.size() == 2 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == 12 &&
            instruction.inputs[1].kind == psxrecomp::ir::ValueKind::REGISTER &&
            instruction.inputs[1].reg == 4)
        {
            foundCtc0Alias = true;
        }
        if (instruction.opcode == Opcode::COP0_MFC && instruction.inputs.size() == 1 &&
            instruction.outputs.size() == 1 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == 12 &&
            instruction.outputs[0].kind == psxrecomp::ir::ValueKind::REGISTER &&
            instruction.outputs[0].reg == 5)
        {
            foundCfc0Alias = true;
        }
    }

    assert(foundCop0Mtc);
    assert(foundCop0Mfc);
    assert(foundCtc0Alias);
    assert(foundCfc0Alias);
    assert(foundCop0Rfe);

    std::vector<psxrecomp::u8> cop2Buffer;
    cop2Buffer.reserve(36);
    appendLe32(cop2Buffer, encodeCop2Transfer(0x04, 8, 6));  // mtc2 $t0, $c2_data6
    appendLe32(cop2Buffer, encodeCop2Transfer(0x00, 9, 6));  // mfc2 $t1, $c2_data6
    appendLe32(cop2Buffer, encodeCop2Transfer(0x06, 10, 7)); // ctc2 $t2, $c2_ctrl7
    appendLe32(cop2Buffer, encodeCop2Transfer(0x02, 11, 7)); // cfc2 $t3, $c2_ctrl7
    appendLe32(cop2Buffer, encodeI(0x32, 12, 8, 0x0010));    // lwc2 $c2_data8, 0x10($t4)
    appendLe32(cop2Buffer, encodeI(0x3A, 13, 9, -4));        // swc2 $c2_data9, -4($t5)
    appendLe32(cop2Buffer, encodeGteCommand(0x280030u));     // rtpt
    appendLe32(cop2Buffer, encodeGteCommand(0x486012u));     // mvmva sf=1, cv=3
    appendLe32(cop2Buffer, encodeR(0, 0, 0, 0, 0x00));       // nop

    auto cop2Instructions =
        MipsDisassembler::disassemble(cop2Buffer.data(), cop2Buffer.size(), 0x80032000);
    auto cop2Result = buildIrFromMips(cop2Instructions);

    assert(cop2Result.errors.empty());
    bool foundGteMtc2 = false;
    bool foundGteMfc2 = false;
    bool foundGteCtc2 = false;
    bool foundGteCfc2 = false;
    bool foundGteLwc2 = false;
    bool foundGteSwc2 = false;
    bool foundGteExecRtpt = false;
    bool foundGteExecMvmva = false;
    for (const auto& instruction : cop2Result.instructions)
    {
        if (instruction.opcode == Opcode::GTE_MTC2 && instruction.inputs.size() == 2 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == 6 &&
            instruction.inputs[1].kind == psxrecomp::ir::ValueKind::REGISTER &&
            instruction.inputs[1].reg == 8)
        {
            foundGteMtc2 = true;
        }
        if (instruction.opcode == Opcode::GTE_MFC2 && instruction.inputs.size() == 1 &&
            instruction.outputs.size() == 1 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == 6 &&
            instruction.outputs[0].kind == psxrecomp::ir::ValueKind::REGISTER &&
            instruction.outputs[0].reg == 9)
        {
            foundGteMfc2 = true;
        }
        if (instruction.opcode == Opcode::GTE_CTC2 && instruction.inputs.size() == 2 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == 7 &&
            instruction.inputs[1].kind == psxrecomp::ir::ValueKind::REGISTER &&
            instruction.inputs[1].reg == 10)
        {
            foundGteCtc2 = true;
        }
        if (instruction.opcode == Opcode::GTE_CFC2 && instruction.inputs.size() == 1 &&
            instruction.outputs.size() == 1 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == 7 &&
            instruction.outputs[0].kind == psxrecomp::ir::ValueKind::REGISTER &&
            instruction.outputs[0].reg == 11)
        {
            foundGteCfc2 = true;
        }
        if (instruction.opcode == Opcode::GTE_LWC2 && instruction.inputs.size() == 2 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == 8 &&
            instruction.inputs[1].kind == psxrecomp::ir::ValueKind::TEMPORARY)
        {
            foundGteLwc2 = true;
        }
        if (instruction.opcode == Opcode::GTE_SWC2 && instruction.inputs.size() == 2 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == 9 &&
            instruction.inputs[1].kind == psxrecomp::ir::ValueKind::TEMPORARY)
        {
            foundGteSwc2 = true;
        }
        if (instruction.opcode == Opcode::GTE_EXEC && instruction.inputs.size() == 1 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == static_cast<psxrecomp::s32>(0x4A280030u))
        {
            foundGteExecRtpt = true;
        }
        if (instruction.opcode == Opcode::GTE_EXEC && instruction.inputs.size() == 1 &&
            instruction.inputs[0].kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs[0].immediate == static_cast<psxrecomp::s32>(0x4A486012u))
        {
            foundGteExecMvmva = true;
        }
    }

    assert(foundGteMtc2);
    assert(foundGteMfc2);
    assert(foundGteCtc2);
    assert(foundGteCfc2);
    assert(foundGteLwc2);
    assert(foundGteSwc2);
    assert(foundGteExecRtpt);
    assert(foundGteExecMvmva);

    std::vector<psxrecomp::u8> cop0ExceptionBuffer;
    cop0ExceptionBuffer.reserve(16);
    appendLe32(cop0ExceptionBuffer, encodeCop0(0x10, 0, 0, 0x02)); // tlbwi
    appendLe32(cop0ExceptionBuffer, encodeI(0x30, 8, 2, 0x0010));  // lwc0 $c2, 0x10($t0)
    appendLe32(cop0ExceptionBuffer, 0xF4B00000u);                  // sdc1 $f16, 0($a1)
    appendLe32(cop0ExceptionBuffer, encodeR(0, 0, 0, 0, 0x00));    // nop

    auto cop0ExceptionInstructions = MipsDisassembler::disassemble(
        cop0ExceptionBuffer.data(), cop0ExceptionBuffer.size(), 0x80031000);
    auto cop0ExceptionResult = buildIrFromMips(cop0ExceptionInstructions);
    assert(cop0ExceptionResult.errors.empty());

    bool foundTlbwiRi = false;
    bool foundLwc0CpU = false;
    bool foundSdc1CpU = false;
    for (const auto& instruction : cop0ExceptionResult.instructions)
    {
        if (instruction.opcode != Opcode::CPU_EXCEPTION || instruction.inputs.size() < 2 ||
            instruction.inputs[0].kind != psxrecomp::ir::ValueKind::IMMEDIATE ||
            instruction.inputs[1].kind != psxrecomp::ir::ValueKind::IMMEDIATE ||
            !instruction.sourceAsmAddress.has_value())
        {
            continue;
        }

        if (instruction.sourceAsmAddress.value() == 0x80031000 &&
            instruction.inputs[0].immediate ==
                static_cast<psxrecomp::s32>(
                    psxrecomp::runtime::Cop0::ExceptionCode::ReservedInstruction))
        {
            foundTlbwiRi = true;
        }
        if (instruction.sourceAsmAddress.value() == 0x80031004 &&
            instruction.inputs[0].immediate ==
                static_cast<psxrecomp::s32>(
                    psxrecomp::runtime::Cop0::ExceptionCode::CoprocessorUnusable))
        {
            foundLwc0CpU = true;
        }
        if (instruction.sourceAsmAddress.value() == 0x80031008 &&
            instruction.inputs[0].immediate ==
                static_cast<psxrecomp::s32>(
                    psxrecomp::runtime::Cop0::ExceptionCode::CoprocessorUnusable))
        {
            foundSdc1CpU = true;
        }
    }

    assert(foundTlbwiRi);
    assert(foundLwc0CpU);
    assert(foundSdc1CpU);

    psxrecomp::ir::MipsIrBuildOptions noSourceAsmOptions;
    noSourceAsmOptions.captureSourceAsm = false;
    auto noSourceAsmResult = buildIrFromMips(instructions, noSourceAsmOptions);
    assert(noSourceAsmResult.errors.empty());
    assert(!noSourceAsmResult.instructions.empty());
    for (const auto& instruction : noSourceAsmResult.instructions)
    {
        assert(!instruction.sourceAsm.has_value());
        assert(!instruction.sourceAsmAddress.has_value());
    }

    return 0;
}
