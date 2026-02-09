#include "psxrecomp/disasm/instruction.h"

#include "instruction_format_helpers.h"

namespace psxrecomp
{
namespace disasm
{

std::string Instruction::toString() const
{
    return toString({});
}

std::string
Instruction::toString(const std::function<std::optional<std::string>(Address)>& labelResolver) const
{
    const auto reg = [](Register r) { return MipsDisassembler::getRegisterName(r); };
    const auto formatRrr = [&](const char* mnemonic)
    { return std::string(mnemonic) + " " + reg(rd) + ", " + reg(rs) + ", " + reg(rt); };
    const auto formatRr = [&](const char* mnemonic, Register lhs, Register rhs)
    { return std::string(mnemonic) + " " + reg(lhs) + ", " + reg(rhs); };
    const auto formatRtRsImmSigned = [&](const char* mnemonic)
    {
        return std::string(mnemonic) + " " + reg(rt) + ", " + reg(rs) + ", " +
               detail::formatImmediateSigned(immediate);
    };
    const auto formatRtRsImmUnsigned = [&](const char* mnemonic)
    {
        return std::string(mnemonic) + " " + reg(rt) + ", " + reg(rs) + ", " +
               detail::formatImmediateUnsigned(static_cast<u16>(immediate));
    };
    const auto formatLoadStore = [&](const char* mnemonic)
    {
        return std::string(mnemonic) + " " + reg(rt) + ", " +
               detail::formatImmediateSigned(immediate) + "(" + reg(rs) + ")";
    };
    const auto formatLoadStoreCop2 = [&](const char* mnemonic)
    {
        return std::string(mnemonic) + " " + detail::formatGteDataRegister(rt) + ", " +
               detail::formatImmediateSigned(immediate) + "(" + reg(rs) + ")";
    };
    const auto formatMove = [&](Register dst, Register src)
    { return std::string("move ") + reg(dst) + ", " + reg(src); };

    switch (opcode)
    {
    case Opcode::ADD:
        return formatRrr("add");
    case Opcode::ADDU:
        if (rt == Registers::ZERO)
        {
            return formatMove(rd, rs);
        }
        if (rs == Registers::ZERO)
        {
            return formatMove(rd, rt);
        }
        return formatRrr("addu");
    case Opcode::SUB:
        if (rs == Registers::ZERO)
        {
            return std::string("neg ") + reg(rd) + ", " + reg(rt);
        }
        return formatRrr("sub");
    case Opcode::SUBU:
        if (rs == Registers::ZERO)
        {
            return std::string("negu ") + reg(rd) + ", " + reg(rt);
        }
        return formatRrr("subu");
    case Opcode::AND:
        return formatRrr("and");
    case Opcode::OR:
        if (rt == Registers::ZERO)
        {
            return formatMove(rd, rs);
        }
        if (rs == Registers::ZERO)
        {
            return formatMove(rd, rt);
        }
        return formatRrr("or");
    case Opcode::XOR:
        return formatRrr("xor");
    case Opcode::NOR:
        if (rs == Registers::ZERO)
        {
            return std::string("not ") + reg(rd) + ", " + reg(rt);
        }
        if (rt == Registers::ZERO)
        {
            return std::string("not ") + reg(rd) + ", " + reg(rs);
        }
        return formatRrr("nor");
    case Opcode::SLT:
        return formatRrr("slt");
    case Opcode::SLTU:
        return formatRrr("sltu");
    case Opcode::SLL:
        if (rd == Registers::ZERO && rt == Registers::ZERO && shamt == 0)
        {
            return "nop";
        }
        return "sll " + reg(rd) + ", " + reg(rt) + ", " + std::to_string(shamt);
    case Opcode::SRL:
        return "srl " + reg(rd) + ", " + reg(rt) + ", " + std::to_string(shamt);
    case Opcode::SRA:
        return "sra " + reg(rd) + ", " + reg(rt) + ", " + std::to_string(shamt);
    case Opcode::SLLV:
        return "sllv " + reg(rd) + ", " + reg(rt) + ", " + reg(rs);
    case Opcode::SRLV:
        return "srlv " + reg(rd) + ", " + reg(rt) + ", " + reg(rs);
    case Opcode::SRAV:
        return "srav " + reg(rd) + ", " + reg(rt) + ", " + reg(rs);
    case Opcode::MULT:
        return formatRr("mult", rs, rt);
    case Opcode::MULTU:
        return formatRr("multu", rs, rt);
    case Opcode::DIV:
        return formatRr("div", rs, rt);
    case Opcode::DIVU:
        return formatRr("divu", rs, rt);
    case Opcode::MFHI:
        return std::string("mfhi ") + reg(rd);
    case Opcode::MTHI:
        return std::string("mthi ") + reg(rs);
    case Opcode::MFLO:
        return std::string("mflo ") + reg(rd);
    case Opcode::MTLO:
        return std::string("mtlo ") + reg(rs);
    case Opcode::JR:
        return std::string("jr ") + reg(rs);
    case Opcode::JALR:
        if (rd == Registers::RA)
        {
            return std::string("jalr ") + reg(rs);
        }
        return formatRr("jalr", rd, rs);
    case Opcode::SYSCALL:
    {
        const u32 code = detail::extractSpecialCode(encoding);
        if (code != 0)
        {
            return std::string("syscall ") + detail::formatHex(code, 5);
        }
        return "syscall";
    }
    case Opcode::BREAK:
    {
        const u32 code = detail::extractSpecialCode(encoding);
        if (code != 0)
        {
            return std::string("break ") + detail::formatHex(code, 5);
        }
        return "break";
    }
    case Opcode::SYNC:
        return "sync";
    case Opcode::TGE:
        return formatRr("tge", rs, rt);
    case Opcode::TGEU:
        return formatRr("tgeu", rs, rt);
    case Opcode::TLT:
        return formatRr("tlt", rs, rt);
    case Opcode::TLTU:
        return formatRr("tltu", rs, rt);
    case Opcode::TEQ:
        return formatRr("teq", rs, rt);
    case Opcode::TNE:
        return formatRr("tne", rs, rt);
    case Opcode::ADDI:
        if (rs == Registers::ZERO)
        {
            return std::string("li ") + reg(rt) + ", " + detail::formatImmediateSigned(immediate);
        }
        return formatRtRsImmSigned("addi");
    case Opcode::ADDIU:
        if (immediate == 0)
        {
            return formatMove(rt, rs);
        }
        if (rs == Registers::ZERO)
        {
            return std::string("li ") + reg(rt) + ", " + detail::formatImmediateSigned(immediate);
        }
        return formatRtRsImmSigned("addiu");
    case Opcode::ANDI:
        return formatRtRsImmUnsigned("andi");
    case Opcode::ORI:
        if (rs == Registers::ZERO)
        {
            return std::string("li ") + reg(rt) + ", " +
                   detail::formatImmediateUnsigned(static_cast<u16>(immediate));
        }
        return formatRtRsImmUnsigned("ori");
    case Opcode::XORI:
        return formatRtRsImmUnsigned("xori");
    case Opcode::SLTI:
        return formatRtRsImmSigned("slti");
    case Opcode::SLTIU:
        return formatRtRsImmSigned("sltiu");
    case Opcode::LUI:
        return std::string("lui ") + reg(rt) + ", " +
               detail::formatImmediateUnsigned(static_cast<u16>(immediate));
    case Opcode::LB:
        return formatLoadStore("lb");
    case Opcode::LH:
        return formatLoadStore("lh");
    case Opcode::LW:
        return formatLoadStore("lw");
    case Opcode::LBU:
        return formatLoadStore("lbu");
    case Opcode::LHU:
        return formatLoadStore("lhu");
    case Opcode::LWL:
        return formatLoadStore("lwl");
    case Opcode::LWR:
        return formatLoadStore("lwr");
    case Opcode::SB:
        return formatLoadStore("sb");
    case Opcode::SH:
        return formatLoadStore("sh");
    case Opcode::SW:
        return formatLoadStore("sw");
    case Opcode::SWL:
        return formatLoadStore("swl");
    case Opcode::SWR:
        return formatLoadStore("swr");
    case Opcode::BEQ:
    case Opcode::BNE:
    case Opcode::BLEZ:
    case Opcode::BGTZ:
    case Opcode::BLTZ:
    case Opcode::BGEZ:
    case Opcode::BLTZAL:
    case Opcode::BGEZAL:
    case Opcode::BC0F:
    case Opcode::BC0T:
    {
        const std::string targetString = detail::resolveTargetString(*this, labelResolver);
        switch (opcode)
        {
        case Opcode::BEQ:
            if (rs == rt)
            {
                return "b " + targetString;
            }
            if (rt == Registers::ZERO)
            {
                return "beqz " + reg(rs) + ", " + targetString;
            }
            return "beq " + reg(rs) + ", " + reg(rt) + ", " + targetString;
        case Opcode::BNE:
            if (rt == Registers::ZERO)
            {
                return "bnez " + reg(rs) + ", " + targetString;
            }
            return "bne " + reg(rs) + ", " + reg(rt) + ", " + targetString;
        case Opcode::BLEZ:
            return "blez " + reg(rs) + ", " + targetString;
        case Opcode::BGTZ:
            return "bgtz " + reg(rs) + ", " + targetString;
        case Opcode::BLTZ:
            return "bltz " + reg(rs) + ", " + targetString;
        case Opcode::BGEZ:
            return "bgez " + reg(rs) + ", " + targetString;
        case Opcode::BLTZAL:
            return "bltzal " + reg(rs) + ", " + targetString;
        case Opcode::BGEZAL:
            return "bgezal " + reg(rs) + ", " + targetString;
        case Opcode::BC0F:
            return "bc0f " + targetString;
        case Opcode::BC0T:
            return "bc0t " + targetString;
        default:
            break;
        }
        break;
    }
    case Opcode::J:
    case Opcode::JAL:
    {
        const std::string targetString = detail::resolveTargetString(*this, labelResolver);
        return (opcode == Opcode::J) ? "j " + targetString : "jal " + targetString;
    }
    case Opcode::TGEI:
        return std::string("tgei ") + reg(rs) + ", " + detail::formatImmediateSigned(immediate);
    case Opcode::TGEIU:
        return std::string("tgeiu ") + reg(rs) + ", " +
               detail::formatImmediateUnsigned(static_cast<u16>(immediate));
    case Opcode::TLTI:
        return std::string("tlti ") + reg(rs) + ", " + detail::formatImmediateSigned(immediate);
    case Opcode::TLTIU:
        return std::string("tltiu ") + reg(rs) + ", " +
               detail::formatImmediateUnsigned(static_cast<u16>(immediate));
    case Opcode::TEQI:
        return std::string("teqi ") + reg(rs) + ", " + detail::formatImmediateSigned(immediate);
    case Opcode::TNEI:
        return std::string("tnei ") + reg(rs) + ", " + detail::formatImmediateSigned(immediate);
    case Opcode::CACHE:
        return std::string("cache ") + detail::formatImmediateUnsigned(static_cast<u16>(rt)) +
               ", " + detail::formatImmediateSigned(immediate) + "(" + reg(rs) + ")";
    case Opcode::MFC0:
        return std::string("mfc0 ") + reg(rt) + ", " + detail::formatCopRegister(rd);
    case Opcode::MTC0:
        return std::string("mtc0 ") + reg(rt) + ", " + detail::formatCopRegister(rd);
    case Opcode::CFC0:
        return std::string("cfc0 ") + reg(rt) + ", " + detail::formatCopRegister(rd);
    case Opcode::CTC0:
        return std::string("ctc0 ") + reg(rt) + ", " + detail::formatCopRegister(rd);
    case Opcode::TLBR:
        return "tlbr";
    case Opcode::TLBWI:
        return "tlbwi";
    case Opcode::TLBWR:
        return "tlbwr";
    case Opcode::TLBP:
        return "tlbp";
    case Opcode::RFE:
        return "rfe";
    case Opcode::LWC0:
        return std::string("lwc0 ") + detail::formatCopRegister(rt) + ", " +
               detail::formatImmediateSigned(immediate) + "(" + reg(rs) + ")";
    case Opcode::SWC0:
        return std::string("swc0 ") + detail::formatCopRegister(rt) + ", " +
               detail::formatImmediateSigned(immediate) + "(" + reg(rs) + ")";
    case Opcode::MFC2:
        return std::string("mfc2 ") + reg(rt) + ", " + detail::formatGteDataRegister(rd);
    case Opcode::MTC2:
        return std::string("mtc2 ") + reg(rt) + ", " + detail::formatGteDataRegister(rd);
    case Opcode::CFC2:
        return std::string("cfc2 ") + reg(rt) + ", " + detail::formatGteControlRegister(rd);
    case Opcode::CTC2:
        return std::string("ctc2 ") + reg(rt) + ", " + detail::formatGteControlRegister(rd);
    case Opcode::GTE_RTPS:
    case Opcode::GTE_RTPT:
    case Opcode::GTE_NCLIP:
    case Opcode::GTE_OP:
    case Opcode::GTE_DPCS:
    case Opcode::GTE_INTPL:
    case Opcode::GTE_MVMVA:
    case Opcode::GTE_NCDS:
    case Opcode::GTE_CDP:
    case Opcode::GTE_NCDT:
    case Opcode::GTE_NCCS:
    case Opcode::GTE_CC:
    case Opcode::GTE_NCS:
    case Opcode::GTE_NCT:
    case Opcode::GTE_SQR:
    case Opcode::GTE_DCPL:
    case Opcode::GTE_DPCT:
    case Opcode::GTE_AVSZ3:
    case Opcode::GTE_AVSZ4:
    case Opcode::GTE_GPF:
    case Opcode::GTE_GPL:
    case Opcode::GTE_NCCT:
        return detail::formatGteCommand(opcode);
    case Opcode::LWC2:
        return formatLoadStoreCop2("lwc2");
    case Opcode::SWC2:
        return formatLoadStoreCop2("swc2");
    case Opcode::UNKNOWN:
    default:
        break;
    }

    return "unknown " + detail::formatHex(encoding, 8);
}

} // namespace disasm
} // namespace psxrecomp
