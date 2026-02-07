#include "psxrecomp/disasm/instruction.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace
{

uint32_t encodeR(uint8_t rs, uint8_t rt, uint8_t rd, uint8_t shamt, uint8_t funct)
{
    return (static_cast<uint32_t>(rs) << 21) | (static_cast<uint32_t>(rt) << 16) |
           (static_cast<uint32_t>(rd) << 11) | (static_cast<uint32_t>(shamt) << 6) | funct;
}

uint32_t encodeI(uint8_t op, uint8_t rs, uint8_t rt, int16_t imm)
{
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(rs) << 21) |
           (static_cast<uint32_t>(rt) << 16) | static_cast<uint16_t>(imm);
}

uint32_t encodeJ(uint8_t op, uint32_t target)
{
    return (static_cast<uint32_t>(op) << 26) | (target & 0x03FFFFFFu);
}

void writeLe32(std::vector<uint8_t>& buffer, size_t offset, uint32_t value)
{
    buffer[offset] = static_cast<uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

} // namespace

int main()
{
    using psxrecomp::Address;
    using psxrecomp::disasm::Instruction;
    using psxrecomp::disasm::InstructionType;
    using psxrecomp::disasm::MipsDisassembler;
    using psxrecomp::disasm::Opcode;

    {
        Instruction add = MipsDisassembler::decode(encodeR(1, 2, 3, 0, 0x20), 0x80010000);
        assert(add.opcode == Opcode::ADD);
        assert(add.type == InstructionType::R_TYPE);
        assert(add.rs == 1);
        assert(add.rt == 2);
        assert(add.rd == 3);
        assert(add.toString() == "add $v1, $at, $v0");

        Instruction move = MipsDisassembler::decode(encodeR(0, 9, 8, 0, 0x25), 0x80010002);
        assert(move.opcode == Opcode::OR);
        assert(move.toString() == "move $t0, $t1");
    }

    {
        Instruction sll = MipsDisassembler::decode(encodeR(0, 9, 8, 4, 0x00), 0x80010004);
        assert(sll.opcode == Opcode::SLL);
        assert(sll.shamt == 4);
        assert(sll.toString() == "sll $t0, $t1, 4");

        Instruction nop = MipsDisassembler::decode(encodeR(0, 0, 0, 0, 0x00), 0x80010006);
        assert(nop.opcode == Opcode::SLL);
        assert(nop.toString() == "nop");
    }

    {
        Instruction addi = MipsDisassembler::decode(encodeI(0x08, 4, 2, -16), 0x80010008);
        assert(addi.opcode == Opcode::ADDI);
        assert(addi.immediate == -16);
        assert(addi.toString() == "addi $v0, $a0, -16");

        Instruction move = MipsDisassembler::decode(encodeI(0x09, 9, 8, 0), 0x8001000A);
        assert(move.opcode == Opcode::ADDIU);
        assert(move.toString() == "move $t0, $t1");
    }

    {
        Instruction andi = MipsDisassembler::decode(encodeI(0x0C, 4, 2, 0x00FF), 0x8001000C);
        assert(andi.opcode == Opcode::ANDI);
        assert(andi.toString() == "andi $v0, $a0, 0x00FF");

        Instruction li = MipsDisassembler::decode(encodeI(0x0D, 0, 8, 0x00FF), 0x8001000E);
        assert(li.opcode == Opcode::ORI);
        assert(li.toString() == "li $t0, 0x00FF");
    }

    {
        Instruction lui = MipsDisassembler::decode(encodeI(0x0F, 0, 8, 0x1234), 0x80010010);
        assert(lui.opcode == Opcode::LUI);
        assert(lui.toString() == "lui $t0, 0x1234");
    }

    {
        Instruction branch = MipsDisassembler::decode(encodeI(0x04, 8, 9, -2), 0x80010014);
        assert(branch.opcode == Opcode::BEQ);
        auto target = branch.getTargetAddress();
        assert(target.has_value());
        assert(*target == 0x80010010);
        assert(branch.toString() == "beq $t0, $t1, 0x80010010");
    }

    {
        Instruction regimm = MipsDisassembler::decode(encodeI(0x01, 8, 0x10, 4), 0x80010018);
        assert(regimm.opcode == Opcode::BLTZAL);
        assert(regimm.isCall());
    }

    {
        Instruction bltz = MipsDisassembler::decode(encodeI(0x01, 9, 0x00, 8), 0x8001001A);
        assert(bltz.opcode == Opcode::BLTZ);
        assert(!bltz.isCall());
        assert(bltz.toString() == "bltz $t1, 0x8001003E");

        Instruction bgez = MipsDisassembler::decode(encodeI(0x01, 10, 0x01, -4), 0x8001001E);
        assert(bgez.opcode == Opcode::BGEZ);
        assert(!bgez.isCall());
        assert(bgez.toString() == "bgez $t2, 0x80010012");
    }

    {
        Instruction jump = MipsDisassembler::decode(encodeJ(0x02, 0x00123456), 0x8001001C);
        assert(jump.opcode == Opcode::J);
        auto target = jump.getTargetAddress();
        assert(target.has_value());
        assert(*target == 0x8048D158);
        assert(jump.toString() == "j 0x8048D158");
    }

    {
        Instruction jr = MipsDisassembler::decode(encodeR(31, 0, 0, 0, 0x08), 0x8001001C);
        assert(jr.opcode == Opcode::JR);
        assert(jr.isJump());
        assert(jr.isReturn());
        assert(jr.toString() == "jr $ra");

        Instruction jalr = MipsDisassembler::decode(encodeR(9, 0, 8, 0, 0x09), 0x80010020);
        assert(jalr.opcode == Opcode::JALR);
        assert(jalr.isCall());
        assert(jalr.toString() == "jalr $t0, $t1");

        Instruction syscall = MipsDisassembler::decode(encodeR(0, 0, 0, 0, 0x0C), 0x80010024);
        assert(syscall.opcode == Opcode::SYSCALL);
        assert(syscall.toString() == "syscall");

        Instruction syscallWithCode =
            MipsDisassembler::decode(encodeR(0, 0, 0, 3, 0x0C) | (0x155u << 6), 0x80010028);
        assert(syscallWithCode.opcode == Opcode::SYSCALL);
        assert(syscallWithCode.toString() == "syscall");

        Instruction brk = MipsDisassembler::decode(encodeR(0, 0, 0, 0, 0x0D), 0x8001002C);
        assert(brk.opcode == Opcode::BREAK);
        assert(brk.toString() == "break");

        Instruction breakWithCode =
            MipsDisassembler::decode(encodeR(0, 0, 0, 1, 0x0D) | (0x2AAu << 6), 0x80010030);
        assert(breakWithCode.opcode == Opcode::BREAK);
        assert(breakWithCode.toString() == "break");
    }

    {
        Instruction mfc0 = MipsDisassembler::decode(
            (0x10u << 26) | (0u << 21) | (2u << 16) | (12u << 11), 0x80010020);
        assert(mfc0.opcode == Opcode::MFC0);
        assert(mfc0.toString() == "mfc0 $v0, $c12");

        Instruction mtc0 = MipsDisassembler::decode(
            (0x10u << 26) | (4u << 21) | (3u << 16) | (7u << 11), 0x80010022);
        assert(mtc0.opcode == Opcode::MTC0);
        assert(mtc0.toString() == "mtc0 $v1, $c7");

        Instruction cfc0 = MipsDisassembler::decode(
            (0x10u << 26) | (2u << 21) | (9u << 16) | (5u << 11), 0x80010024);
        assert(cfc0.opcode == Opcode::CFC0);
        assert(cfc0.toString() == "cfc0 $t1, $c5");

        Instruction ctc0 = MipsDisassembler::decode(
            (0x10u << 26) | (6u << 21) | (10u << 16) | (4u << 11), 0x80010026);
        assert(ctc0.opcode == Opcode::CTC0);
        assert(ctc0.toString() == "ctc0 $t2, $c4");

        Instruction rfe =
            MipsDisassembler::decode((0x10u << 26) | (0x10u << 21) | 0x10u, 0x80010024);
        assert(rfe.opcode == Opcode::RFE);
        assert(rfe.toString() == "rfe");

        Instruction tlbp =
            MipsDisassembler::decode((0x10u << 26) | (0x10u << 21) | 0x08u, 0x80010028);
        assert(tlbp.opcode == Opcode::TLBP);
        assert(tlbp.toString() == "tlbp");

        Instruction tlbr =
            MipsDisassembler::decode((0x10u << 26) | (0x10u << 21) | 0x01u, 0x8001002C);
        assert(tlbr.opcode == Opcode::TLBR);
        assert(tlbr.toString() == "tlbr");

        Instruction tlbwi =
            MipsDisassembler::decode((0x10u << 26) | (0x10u << 21) | 0x02u, 0x80010030);
        assert(tlbwi.opcode == Opcode::TLBWI);
        assert(tlbwi.toString() == "tlbwi");

        Instruction tlbwr =
            MipsDisassembler::decode((0x10u << 26) | (0x10u << 21) | 0x06u, 0x80010034);
        assert(tlbwr.opcode == Opcode::TLBWR);
        assert(tlbwr.toString() == "tlbwr");

        Instruction cop0Unknown =
            MipsDisassembler::decode((0x10u << 26) | (0x10u << 21) | 0x0Fu, 0x80010038);
        assert(cop0Unknown.opcode == Opcode::UNKNOWN);
        assert(cop0Unknown.toString() == "unknown 0x4200000F");
    }

    {
        Instruction mfc2 = MipsDisassembler::decode(
            (0x12u << 26) | (0u << 21) | (3u << 16) | (5u << 11), 0x80010026);
        assert(mfc2.opcode == Opcode::MFC2);
        assert(mfc2.toString() == "mfc2 $v1, $vz2");

        Instruction mtc2 = MipsDisassembler::decode(
            (0x12u << 26) | (4u << 21) | (9u << 16) | (7u << 11), 0x8001002A);
        assert(mtc2.opcode == Opcode::MTC2);
        assert(mtc2.toString() == "mtc2 $t1, $otz");

        Instruction cfc2 = MipsDisassembler::decode(
            (0x12u << 26) | (2u << 21) | (10u << 16) | (4u << 11), 0x8001002E);
        assert(cfc2.opcode == Opcode::CFC2);
        assert(cfc2.toString() == "cfc2 $t2, $r33");

        Instruction gteRtpt =
            MipsDisassembler::decode((0x12u << 26) | (0x10u << 21) | 0x01u, 0x80010032);
        assert(gteRtpt.opcode == Opcode::GTE_RTPT);
        assert(gteRtpt.toString() == "rtpt");
    }

    {
        Instruction lwc2 = MipsDisassembler::decode(encodeI(0x32, 4, 6, 12), 0x80010028);
        assert(lwc2.opcode == Opcode::LWC2);
        assert(lwc2.toString() == "lwc2 $rgbc, 12($a0)");

        Instruction swc2 = MipsDisassembler::decode(encodeI(0x3A, 5, 7, -4), 0x8001002C);
        assert(swc2.opcode == Opcode::SWC2);
        assert(swc2.toString() == "swc2 $otz, -4($a1)");
    }

    {
        Instruction lwl = MipsDisassembler::decode(encodeI(0x22, 4, 8, 2), 0x80010040);
        assert(lwl.opcode == Opcode::LWL);
        assert(lwl.toString() == "lwl $t0, 2($a0)");

        Instruction lwr = MipsDisassembler::decode(encodeI(0x26, 4, 8, -2), 0x80010044);
        assert(lwr.opcode == Opcode::LWR);
        assert(lwr.toString() == "lwr $t0, -2($a0)");

        Instruction swl = MipsDisassembler::decode(encodeI(0x2A, 4, 8, 6), 0x80010048);
        assert(swl.opcode == Opcode::SWL);
        assert(swl.toString() == "swl $t0, 6($a0)");

        Instruction swr = MipsDisassembler::decode(encodeI(0x2E, 4, 8, -6), 0x8001004C);
        assert(swr.opcode == Opcode::SWR);
        assert(swr.toString() == "swr $t0, -6($a0)");
    }

    {
        std::vector<uint32_t> encodings = {encodeI(0x04, 8, 9, 1), encodeR(0, 0, 0, 0, 0x00)};

        std::vector<uint8_t> bytes(encodings.size() * sizeof(uint32_t), 0);
        for (size_t i = 0; i < encodings.size(); ++i)
        {
            writeLe32(bytes, i * sizeof(uint32_t), encodings[i]);
        }

        auto instructions = MipsDisassembler::disassemble(bytes.data(), bytes.size(), 0x80010030);
        assert(instructions.size() == 2);
        assert(!instructions[0].isInDelaySlot);
        assert(instructions[1].isInDelaySlot);
        assert(instructions[1].delaySlotOwner.has_value());
        assert(*instructions[1].delaySlotOwner == 0x80010030);
    }

    {
        std::vector<uint32_t> encodings = {
            encodeI(0x04, 8, 9, 1),
            encodeI(0x05, 8, 9, 1),
            encodeR(0, 0, 0, 0, 0x00),
        };

        std::vector<uint8_t> bytes(encodings.size() * sizeof(uint32_t), 0);
        for (size_t i = 0; i < encodings.size(); ++i)
        {
            writeLe32(bytes, i * sizeof(uint32_t), encodings[i]);
        }

        auto instructions = MipsDisassembler::disassemble(bytes.data(), bytes.size(), 0x80010034);
        assert(instructions.size() == 3);
        assert(!instructions[0].isInDelaySlot);
        assert(instructions[1].isInDelaySlot);
        assert(instructions[2].isInDelaySlot);
        assert(instructions[1].delaySlotOwner.has_value());
        assert(*instructions[1].delaySlotOwner == 0x80010034);
        assert(instructions[2].delaySlotOwner.has_value());
        assert(*instructions[2].delaySlotOwner == 0x80010038);
    }

    {
        Instruction unknown = MipsDisassembler::decode(0xFC000000u, 0x80010038);
        assert(unknown.opcode == Opcode::UNKNOWN);
        assert(unknown.toString() == "unknown 0xFC000000");
    }

    {
        Instruction backward = MipsDisassembler::decode(encodeI(0x05, 8, 9, -1), 0x80010050);
        auto target = backward.getTargetAddress();
        assert(target.has_value());
        assert(*target == 0x80010050);
        assert(backward.toString() == "bne $t0, $t1, 0x80010050");
    }

    return 0;
}
