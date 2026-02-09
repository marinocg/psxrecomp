#include "instruction_format_helpers.h"

#include <array>
#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace disasm
{
namespace detail
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
    // clang-format off
    static constexpr std::array<const char*, 32> names = {
        "$vxy0", "$vz0",  "$vxy1", "$vz1",  "$vxy2", "$vz2",  "$rgbc", "$otz",
        "$ir0",  "$ir1",  "$ir2",  "$ir3",  "$sxy0", "$sxy1", "$sxy2", "$sxyp",
        "$sz0",  "$sz1",  "$sz2",  "$sz3",  "$rgb0", "$rgb1", "$rgb2", "$res1",
        "$mac0", "$mac1", "$mac2", "$mac3", "$irgb", "$orgb", "$lzcs", "$lzcr"};
    // clang-format on

    if (reg < names.size())
    {
        return names[reg];
    }

    return formatCopRegister(reg);
}

std::string formatGteControlRegister(u8 reg)
{
    // clang-format off
    static constexpr std::array<const char*, 32> names = {
        "$r11r12", "$r13r21", "$r22r23", "$r31r32", "$r33", "$trx",  "$try",  "$trz",
        "$l11l12", "$l13l21", "$l22l23", "$l31l32", "$l33", "$rbk",  "$gbk",  "$bbk",
        "$lr1lr2", "$lr3lg1", "$lg2lg3", "$lb1lb2", "$lb3", "$rfc",  "$gfc",  "$bfc",
        "$ofx",    "$ofy",    "$h",      "$dqa",    "$dqb", "$zsf3", "$zsf4", "$flag"};
    // clang-format on

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

u32 extractSpecialCode(u32 encoding)
{
    return (encoding >> 6) & 0xFFFFFu;
}

std::string
resolveTargetString(const Instruction& instruction,
                    const std::function<std::optional<std::string>(Address)>& labelResolver)
{
    auto targetAddress = instruction.getTargetAddress();
    if (!targetAddress)
    {
        return "?";
    }

    if (labelResolver)
    {
        if (auto label = labelResolver(*targetAddress))
        {
            return *label;
        }
    }

    return formatHex(*targetAddress, 8);
}

} // namespace detail
} // namespace disasm
} // namespace psxrecomp
