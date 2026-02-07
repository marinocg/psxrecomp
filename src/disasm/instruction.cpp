#include "psxrecomp/disasm/instruction.h"

#include <array>
#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace disasm
{

namespace
{
std::string formatHex(u32 value, int width)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value;
    return stream.str();
}

std::string formatImmediateSigned(s16 value)
{
    return std::to_string(value);
}

std::string formatImmediateUnsigned(u16 value)
{
    return formatHex(value, 4);
}

std::string formatCopRegister(u8 reg)
{
    return "$c" + std::to_string(reg);
}

std::string formatGteDataRegister(u8 reg)
{
    static constexpr std::array<const char*, 32> names = {
        "$vxy0", "$vz0",  "$vxy1", "$vz1", "$vxy2", "$vz2",  "$rgbc", "$otz",
        "$ir0",  "$ir1",  "$ir2",  "$ir3", "$sxy0", "$sxy1", "$sxy2", "$sxyp",
        "$sz0",  "$sz1",  "$sz2",  "$sz3", "$rgb0", "$rgb1", "$rgb2", "$res1",
        "$mac0", "$mac1", "$mac2", "$mac3", "$irgb", "$orgb", "$lzcs", "$lzcr"};

    if (reg < names.size())
    {
        return names[reg];
    }

    return formatCopRegister(reg);
}

std::string formatGteControlRegister(u8 reg)
{
    static constexpr std::array<const char*, 32> names = {
        "$r11r12", "$r13r21", "$r22r23", "$r31r32", "$r33", "$trx", "$try",
        "$trz",    "$l11l12", "$l13l21", "$l22l23", "$l31l32", "$l33", "$rbk",
        "$gbk",    "$bbk",    "$lr1lr2", "$lr3lg1", "$lg2lg3", "$lb1lb2", "$lb3",
        "$rfc",    "$gfc",    "$bfc",    "$ofx",    "$ofy",    "$h",      "$dqa",
        "$dqb",    "$zsf3",   "$zsf4",   "$flag"};

    if (reg < names.size())
    {
        return names[reg];
    }

    return formatCopRegister(reg);
}

std::string formatGteCommand(Opcode opcode)
{
    switch (opcode)
    {
    case Opcode::GTE_RTPS:
        return "rtps";
    case Opcode::GTE_RTPT:
        return "rtpt";
    case Opcode::GTE_NCLIP:
        return "nclip";
    case Opcode::GTE_OP:
        return "op";
    case Opcode::GTE_DPCS:
        return "dpcs";
    case Opcode::GTE_INTPL:
        return "intpl";
    case Opcode::GTE_MVMVA:
        return "mvmva";
    case Opcode::GTE_NCDS:
        return "ncds";
    case Opcode::GTE_CDP:
        return "cdp";
    case Opcode::GTE_NCDT:
        return "ncdt";
    case Opcode::GTE_NCCS:
        return "nccs";
    case Opcode::GTE_CC:
        return "cc";
    case Opcode::GTE_NCS:
        return "ncs";
    case Opcode::GTE_NCT:
        return "nct";
    case Opcode::GTE_SQR:
        return "sqr";
    case Opcode::GTE_DCPL:
        return "dcpl";
    case Opcode::GTE_DPCT:
        return "dpct";
    case Opcode::GTE_AVSZ3:
        return "avsz3";
    case Opcode::GTE_AVSZ4:
        return "avsz4";
    case Opcode::GTE_GPF:
        return "gpf";
    case Opcode::GTE_GPL:
        return "gpl";
    case Opcode::GTE_NCCT:
        return "ncct";
    default:
        return {};
    }
}
} // namespace

std::string Instruction::toString() const
{
    const auto reg = [](Register r) { return MipsDisassembler::getRegisterName(r); };
    const auto formatRrr = [&](const char* mnemonic) {
        return std::string(mnemonic) + " " + reg(rd) + ", " + reg(rs) + ", " + reg(rt);
    };
    const auto formatRr = [&](const char* mnemonic, Register lhs, Register rhs) {
        return std::string(mnemonic) + " " + reg(lhs) + ", " + reg(rhs);
    };
    const auto formatRtRsImmSigned = [&](const char* mnemonic) {
        return std::string(mnemonic) + " " + reg(rt) + ", " + reg(rs) + ", " +
               formatImmediateSigned(immediate);
    };
    const auto formatRtRsImmUnsigned = [&](const char* mnemonic) {
        return std::string(mnemonic) + " " + reg(rt) + ", " + reg(rs) + ", " +
               formatImmediateUnsigned(static_cast<u16>(immediate));
    };
    const auto formatLoadStore = [&](const char* mnemonic) {
        return std::string(mnemonic) + " " + reg(rt) + ", " + formatImmediateSigned(immediate) +
               "(" + reg(rs) + ")";
    };
    const auto formatLoadStoreCop2 = [&](const char* mnemonic) {
        return std::string(mnemonic) + " " + formatGteDataRegister(rt) + ", " +
               formatImmediateSigned(immediate) + "(" + reg(rs) + ")";
    };
    const auto formatMove = [&](Register dst, Register src) {
        return std::string("move ") + reg(dst) + ", " + reg(src);
    };

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
        return formatRrr("sub");
    case Opcode::SUBU:
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
        return formatRr("jalr", rd, rs);
    case Opcode::SYSCALL:
        return "syscall";
    case Opcode::BREAK:
        return "break";
    case Opcode::ADDI:
        return formatRtRsImmSigned("addi");
    case Opcode::ADDIU:
        if (immediate == 0)
        {
            return formatMove(rt, rs);
        }
        return formatRtRsImmSigned("addiu");
    case Opcode::ANDI:
        return formatRtRsImmUnsigned("andi");
    case Opcode::ORI:
        if (rs == Registers::ZERO)
        {
            return std::string("li ") + reg(rt) + ", " +
                   formatImmediateUnsigned(static_cast<u16>(immediate));
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
               formatImmediateUnsigned(static_cast<u16>(immediate));
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
    case Opcode::BGEZAL: {
        auto targetAddress = getTargetAddress();
        const std::string targetString = targetAddress ? formatHex(*targetAddress, 8) : "?";
        switch (opcode)
        {
        case Opcode::BEQ:
            return "beq " + reg(rs) + ", " + reg(rt) + ", " + targetString;
        case Opcode::BNE:
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
        default:
            break;
        }
        break;
    }
    case Opcode::J:
    case Opcode::JAL: {
        auto targetAddress = getTargetAddress();
        const std::string targetString = targetAddress ? formatHex(*targetAddress, 8) : "?";
        return (opcode == Opcode::J) ? "j " + targetString : "jal " + targetString;
    }
    case Opcode::MFC0:
        return std::string("mfc0 ") + reg(rt) + ", " + formatCopRegister(rd);
    case Opcode::MTC0:
        return std::string("mtc0 ") + reg(rt) + ", " + formatCopRegister(rd);
    case Opcode::CFC0:
        return std::string("cfc0 ") + reg(rt) + ", " + formatCopRegister(rd);
    case Opcode::CTC0:
        return std::string("ctc0 ") + reg(rt) + ", " + formatCopRegister(rd);
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
    case Opcode::MFC2:
        return std::string("mfc2 ") + reg(rt) + ", " + formatGteDataRegister(rd);
    case Opcode::MTC2:
        return std::string("mtc2 ") + reg(rt) + ", " + formatGteDataRegister(rd);
    case Opcode::CFC2:
        return std::string("cfc2 ") + reg(rt) + ", " + formatGteControlRegister(rd);
    case Opcode::CTC2:
        return std::string("ctc2 ") + reg(rt) + ", " + formatGteControlRegister(rd);
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
        return formatGteCommand(opcode);
    case Opcode::LWC2:
        return formatLoadStoreCop2("lwc2");
    case Opcode::SWC2:
        return formatLoadStoreCop2("swc2");
    case Opcode::UNKNOWN:
    default:
        break;
    }

    return "unknown " + formatHex(encoding, 8);
}

bool Instruction::isBranch() const
{
    switch (opcode)
    {
    case Opcode::BEQ:
    case Opcode::BNE:
    case Opcode::BLEZ:
    case Opcode::BGTZ:
    case Opcode::BLTZ:
    case Opcode::BGEZ:
    case Opcode::BLTZAL:
    case Opcode::BGEZAL:
        return true;
    default:
        return false;
    }
}

bool Instruction::isJump() const
{
    switch (opcode)
    {
    case Opcode::J:
    case Opcode::JAL:
    case Opcode::JR:
    case Opcode::JALR:
        return true;
    default:
        return false;
    }
}

bool Instruction::isCall() const
{
    return opcode == Opcode::JAL || opcode == Opcode::JALR || opcode == Opcode::BLTZAL ||
           opcode == Opcode::BGEZAL;
}

bool Instruction::isReturn() const
{
    return opcode == Opcode::JR && rs == Registers::RA;
}

std::optional<Address> Instruction::getTargetAddress() const
{
    if (isBranch())
    {
        const s32 offset = static_cast<s32>(immediate) << 2;
        return static_cast<Address>(address + 4 + offset);
    }

    if (opcode == Opcode::J || opcode == Opcode::JAL)
    {
        const Address base = (address + 4) & 0xF0000000u;
        return base | (target << 2);
    }

    return std::nullopt;
}

} // namespace disasm
} // namespace psxrecomp
