#include "psxrecomp/disasm/instruction.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
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
        Instruction beqz = MipsDisassembler::decode(encodeI(0x04, 8, 0, 1), 0x80010050);
        assert(beqz.toString() == "beqz $t0, 0x80010058");

        Instruction bnez = MipsDisassembler::decode(encodeI(0x05, 8, 0, 1), 0x80010054);
        assert(bnez.toString() == "bnez $t0, 0x8001005C");

        Instruction b = MipsDisassembler::decode(encodeI(0x04, 8, 8, 1), 0x80010058);
        assert(b.toString() == "b 0x80010060");
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

        Instruction tgei = MipsDisassembler::decode(encodeI(0x01, 8, 0x08, -1), 0x80010040);
        assert(tgei.opcode == Opcode::TGEI);
        assert(tgei.toString() == "tgei $t0, -1");

        Instruction tltiu = MipsDisassembler::decode(encodeI(0x01, 9, 0x0B, 7), 0x80010044);
        assert(tltiu.opcode == Opcode::TLTIU);
        assert(tltiu.toString() == "tltiu $t1, 0x0007");
    }

    {
        Instruction jump = MipsDisassembler::decode(encodeJ(0x02, 0x00123456), 0x8001001C);
        assert(jump.opcode == Opcode::J);
        auto target = jump.getTargetAddress();
        assert(target.has_value());
        assert(*target == 0x8048D158);
        assert(jump.toString() == "j 0x8048D158");

        auto jumpTarget = jump.getJumpTarget();
        assert(jumpTarget.has_value());
        assert(*jumpTarget == 0x8048D158);
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

        Instruction jalrRa = MipsDisassembler::decode(encodeR(9, 0, 31, 0, 0x09), 0x80010020);
        assert(jalrRa.opcode == Opcode::JALR);
        assert(jalrRa.toString() == "jalr $t1");

        Instruction syscall = MipsDisassembler::decode(encodeR(0, 0, 0, 0, 0x0C), 0x80010024);
        assert(syscall.opcode == Opcode::SYSCALL);
        assert(syscall.toString() == "syscall");

        Instruction syscallWithCode =
            MipsDisassembler::decode(encodeR(0, 0, 0, 0, 0x0C) | (0x155u << 6), 0x80010028);
        assert(syscallWithCode.opcode == Opcode::SYSCALL);
        assert(syscallWithCode.toString() == "syscall 0x00155");

        Instruction brk = MipsDisassembler::decode(encodeR(0, 0, 0, 0, 0x0D), 0x8001002C);
        assert(brk.opcode == Opcode::BREAK);
        assert(brk.toString() == "break");

        Instruction breakWithCode =
            MipsDisassembler::decode(encodeR(0, 0, 0, 0, 0x0D) | (0x2AAu << 6), 0x80010030);
        assert(breakWithCode.opcode == Opcode::BREAK);
        assert(breakWithCode.toString() == "break 0x002AA");

        Instruction sync = MipsDisassembler::decode(encodeR(0, 0, 0, 0, 0x0F), 0x80010032);
        assert(sync.opcode == Opcode::SYNC);
        assert(sync.toString() == "sync");

        Instruction tge = MipsDisassembler::decode(encodeR(8, 9, 0, 0, 0x30), 0x80010034);
        assert(tge.opcode == Opcode::TGE);
        assert(tge.toString() == "tge $t0, $t1");

        Instruction tltu = MipsDisassembler::decode(encodeR(10, 11, 0, 0, 0x33), 0x80010036);
        assert(tltu.opcode == Opcode::TLTU);
        assert(tltu.toString() == "tltu $t2, $t3");
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

        Instruction bc0f = MipsDisassembler::decode(encodeI(0x10, 0x08, 0x00, 2), 0x8001003C);
        assert(bc0f.opcode == Opcode::BC0F);
        assert(bc0f.toString() == "bc0f 0x80010048");

        Instruction bc0t = MipsDisassembler::decode(encodeI(0x10, 0x08, 0x01, -1), 0x80010040);
        assert(bc0t.opcode == Opcode::BC0T);
        assert(bc0t.toString() == "bc0t 0x80010040");

        Instruction lwc0 = MipsDisassembler::decode(encodeI(0x30, 8, 4, 12), 0x80010044);
        assert(lwc0.opcode == Opcode::LWC0);
        assert(lwc0.toString() == "lwc0 $c4, 12($t0)");

        Instruction swc0 = MipsDisassembler::decode(encodeI(0x38, 9, 5, -8), 0x80010048);
        assert(swc0.opcode == Opcode::SWC0);
        assert(swc0.toString() == "swc0 $c5, -8($t1)");
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

        Instruction gteRtps = MipsDisassembler::decode(0x4A180001u, 0x80010032);
        assert(gteRtps.opcode == Opcode::GTE_RTPS);
        assert(gteRtps.toString() == "rtps");

        Instruction gteRtpt = MipsDisassembler::decode(0x4A280030u, 0x80010036);
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

        Instruction cache = MipsDisassembler::decode(encodeI(0x2F, 8, 0x1F, 16), 0x80010050);
        assert(cache.opcode == Opcode::CACHE);
        assert(cache.toString() == "cache 0x001F, 16($t0)");
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
        Instruction neg = MipsDisassembler::decode(encodeR(0, 9, 8, 0, 0x22), 0x80010060);
        assert(neg.toString() == "neg $t0, $t1");

        Instruction negu = MipsDisassembler::decode(encodeR(0, 9, 8, 0, 0x23), 0x80010064);
        assert(negu.toString() == "negu $t0, $t1");

        Instruction notInstr = MipsDisassembler::decode(encodeR(9, 0, 8, 0, 0x27), 0x80010068);
        assert(notInstr.toString() == "not $t0, $t1");
    }

    {
        Instruction backward = MipsDisassembler::decode(encodeI(0x05, 8, 9, -1), 0x80010050);
        auto target = backward.getTargetAddress();
        assert(target.has_value());
        assert(*target == 0x80010050);
        assert(backward.toString() == "bne $t0, $t1, 0x80010050");
    }

    {
        Instruction load = MipsDisassembler::decode(encodeI(0x23, 8, 9, 4), 0x80010070);
        assert(load.getMemoryAccessType() == psxrecomp::disasm::MemoryAccessType::LOAD);
        assert(load.getMemoryAccessSize() == psxrecomp::disasm::MemoryAccessSize::WORD);
        assert(load.getAddressingMode() == psxrecomp::disasm::AddressingMode::BASE_OFFSET);

        Instruction branch = MipsDisassembler::decode(encodeI(0x05, 8, 9, 2), 0x80010074);
        assert(branch.getAddressingMode() == psxrecomp::disasm::AddressingMode::PC_RELATIVE);
        assert(branch.getBranchTarget() == 0x80010080);

        Instruction jump = MipsDisassembler::decode(encodeJ(0x02, 0x00100000), 0x80010078);
        assert(jump.getAddressingMode() == psxrecomp::disasm::AddressingMode::ABSOLUTE);

        Instruction jr = MipsDisassembler::decode(encodeR(31, 0, 0, 0, 0x08), 0x8001007C);
        assert(jr.getAddressingMode() == psxrecomp::disasm::AddressingMode::REGISTER);
    }

    {
        Instruction labelBranch = MipsDisassembler::decode(encodeI(0x04, 8, 9, 1), 0x80010080);
        auto labelString = labelBranch.toString(
            [](Address address) -> std::optional<std::string>
            {
                if (address == 0x80010088)
                {
                    return std::string("label_80010088");
                }
                return std::nullopt;
            });
        assert(labelString == "beq $t0, $t1, label_80010088");
    }

    {
        std::vector<uint32_t> encodings = {
            encodeI(0x0F, 0, 4, 0x1F80), // lui $a0, 0x1F80
            encodeI(0x0D, 4, 4, 0x0010), // ori $a0, $a0, 0x0010
            encodeI(0x09, 0, 5, 0x0034), // addiu $a1, $zero, 0x0034
            encodeI(0x2B, 4, 5, 0),      // sw $a1, 0($a0)
            encodeI(0x04, 5, 0, 2),      // beqz $a1, +2
            encodeR(31, 0, 0, 0, 0x08),  // jr $ra
            encodeR(0, 0, 0, 0, 0x00),   // nop
            0x4A180001u,                 // gte rtps
            encodeI(0x10, 0x08, 0x01, 1) // bc0t
        };

        std::vector<uint8_t> bytes(encodings.size() * sizeof(uint32_t), 0);
        for (size_t i = 0; i < encodings.size(); ++i)
        {
            writeLe32(bytes, i * sizeof(uint32_t), encodings[i]);
        }

        auto instructions = MipsDisassembler::disassemble(bytes.data(), bytes.size(), 0x80020000);
        assert(instructions.size() == encodings.size());
        assert(instructions[0].toString() == "lui $a0, 0x1F80");
        assert(instructions[1].toString() == "ori $a0, $a0, 0x0010");
        assert(instructions[2].toString() == "li $a1, 52");
        assert(instructions[3].toString() == "sw $a1, 0($a0)");
        assert(instructions[4].toString() == "beqz $a1, 0x8002001C");
        assert(instructions[5].toString() == "jr $ra");
        assert(instructions[6].toString() == "nop");
        assert(instructions[7].toString() == "rtps");
        assert(instructions[8].toString() == "bc0t 0x80020028");
    }

    {
        std::vector<uint32_t> invalidEncodings = {
            0xFFFFFFFFu,
            0xFC000000u,
            0xF4000000u,
            0x4200000Fu,
        };

        for (size_t i = 0; i < invalidEncodings.size(); ++i)
        {
            Instruction invalid =
                MipsDisassembler::decode(invalidEncodings[i], 0x80030000 + (i * 4));
            assert(invalid.opcode == Opcode::UNKNOWN);
            assert(invalid.toString().rfind("unknown", 0) == 0);
        }
    }

    return 0;
}
