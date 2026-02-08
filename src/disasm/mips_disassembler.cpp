#include "psxrecomp/disasm/instruction.h"

#include <array>

namespace psxrecomp
{
namespace disasm
{

namespace
{
constexpr u32 OPCODE_MASK = 0xFC000000;
constexpr u32 RS_MASK = 0x03E00000;
constexpr u32 RT_MASK = 0x001F0000;
constexpr u32 RD_MASK = 0x0000F800;
constexpr u32 SHAMT_MASK = 0x000007C0;
constexpr u32 FUNCT_MASK = 0x0000003F;
constexpr u32 IMM_MASK = 0x0000FFFF;
constexpr u32 TARGET_MASK = 0x03FFFFFF;

struct OpcodeMapEntry
{
    u32 encoding;
    Opcode opcode;
};

Instruction makeBaseInstruction(u32 encoding, Address address)
{
    Instruction instruction{};
    instruction.address = address;
    instruction.encoding = encoding;
    instruction.opcode = Opcode::UNKNOWN;
    instruction.type = InstructionType::UNKNOWN;
    instruction.rd = 0;
    instruction.rs = 0;
    instruction.rt = 0;
    instruction.immediate = 0;
    instruction.target = 0;
    instruction.shamt = 0;
    instruction.isInDelaySlot = false;
    instruction.delaySlotOwner = std::nullopt;
    return instruction;
}

Register extractField(u32 encoding, u32 mask, int shift)
{
    return static_cast<Register>((encoding & mask) >> shift);
}

u32 readLittleEndianU32(const u8* data)
{
    return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
           (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
}

template <size_t N>
bool decodeWithTable(const std::array<OpcodeMapEntry, N>& table, u32 encoding, Opcode& outOpcode)
{
    for (const auto& entry : table)
    {
        if (entry.encoding == encoding)
        {
            outOpcode = entry.opcode;
            return true;
        }
    }
    return false;
}

// clang-format off
constexpr std::array<OpcodeMapEntry, 35> kRTypeTable = {{
    {0x20, Opcode::ADD},  {0x21, Opcode::ADDU},  {0x22, Opcode::SUB},     {0x23, Opcode::SUBU},
    {0x24, Opcode::AND},  {0x25, Opcode::OR},    {0x26, Opcode::XOR},     {0x27, Opcode::NOR},
    {0x2A, Opcode::SLT},  {0x2B, Opcode::SLTU},  {0x00, Opcode::SLL},     {0x02, Opcode::SRL},
    {0x03, Opcode::SRA},  {0x04, Opcode::SLLV},  {0x06, Opcode::SRLV},    {0x07, Opcode::SRAV},
    {0x18, Opcode::MULT}, {0x19, Opcode::MULTU}, {0x1A, Opcode::DIV},     {0x1B, Opcode::DIVU},
    {0x10, Opcode::MFHI}, {0x11, Opcode::MTHI},  {0x12, Opcode::MFLO},    {0x13, Opcode::MTLO},
    {0x08, Opcode::JR},   {0x09, Opcode::JALR},  {0x0C, Opcode::SYSCALL}, {0x0D, Opcode::BREAK},
    {0x0F, Opcode::SYNC}, {0x30, Opcode::TGE},   {0x31, Opcode::TGEU},    {0x32, Opcode::TLT},
    {0x33, Opcode::TLTU}, {0x34, Opcode::TEQ},   {0x36, Opcode::TNE},
}};
// clang-format on

// clang-format off
constexpr std::array<OpcodeMapEntry, 25> kITypeTable = {{
    {0x08, Opcode::ADDI}, {0x09, Opcode::ADDIU}, {0x0C, Opcode::ANDI},  {0x0D, Opcode::ORI},
    {0x0E, Opcode::XORI}, {0x0A, Opcode::SLTI},  {0x0B, Opcode::SLTIU}, {0x0F, Opcode::LUI},
    {0x20, Opcode::LB},   {0x21, Opcode::LH},    {0x23, Opcode::LW},    {0x24, Opcode::LBU},
    {0x25, Opcode::LHU},  {0x22, Opcode::LWL},   {0x26, Opcode::LWR},   {0x28, Opcode::SB},
    {0x29, Opcode::SH},   {0x2B, Opcode::SW},    {0x2A, Opcode::SWL},   {0x2E, Opcode::SWR},
    {0x04, Opcode::BEQ},  {0x05, Opcode::BNE},   {0x06, Opcode::BLEZ},  {0x07, Opcode::BGTZ},
    {0x2F, Opcode::CACHE},
}};
// clang-format on

constexpr std::array<OpcodeMapEntry, 4> kRegimmTable = {{
    {0x00, Opcode::BLTZ},
    {0x01, Opcode::BGEZ},
    {0x10, Opcode::BLTZAL},
    {0x11, Opcode::BGEZAL},
}};

constexpr std::array<OpcodeMapEntry, 6> kRegimmTrapTable = {{
    {0x08, Opcode::TGEI},
    {0x09, Opcode::TGEIU},
    {0x0A, Opcode::TLTI},
    {0x0B, Opcode::TLTIU},
    {0x0C, Opcode::TEQI},
    {0x0E, Opcode::TNEI},
}};

// clang-format off
constexpr std::array<OpcodeMapEntry, 22> kCop2CommandTable = {{
    {0x00, Opcode::GTE_RTPS},  {0x01, Opcode::GTE_RTPT}, {0x06, Opcode::GTE_NCLIP},
    {0x0C, Opcode::GTE_OP},    {0x10, Opcode::GTE_DPCS}, {0x11, Opcode::GTE_INTPL},
    {0x12, Opcode::GTE_MVMVA}, {0x13, Opcode::GTE_NCDS}, {0x14, Opcode::GTE_CDP},
    {0x16, Opcode::GTE_NCDT},  {0x1B, Opcode::GTE_NCCS}, {0x1C, Opcode::GTE_CC},
    {0x1E, Opcode::GTE_NCS},   {0x20, Opcode::GTE_NCT},  {0x28, Opcode::GTE_SQR},
    {0x29, Opcode::GTE_DCPL},  {0x2A, Opcode::GTE_DPCT}, {0x2D, Opcode::GTE_AVSZ3},
    {0x2E, Opcode::GTE_AVSZ4}, {0x3D, Opcode::GTE_GPF},  {0x3E, Opcode::GTE_GPL},
    {0x3F, Opcode::GTE_NCCT},
}};
// clang-format on
} // namespace

Instruction MipsDisassembler::decode(u32 encoding, Address address)
{
    const u32 opcode = (encoding & OPCODE_MASK) >> 26;

    if (opcode == 0x00)
    {
        return decodeRType(encoding, address);
    }

    if (opcode == 0x02 || opcode == 0x03)
    {
        return decodeJType(encoding, address);
    }

    if (opcode == 0x10 || opcode == 0x12 || opcode == 0x30 || opcode == 0x32 || opcode == 0x38 ||
        opcode == 0x3A)
    {
        return decodeCoprocessor(encoding, address);
    }

    return decodeIType(encoding, address);
}

Instruction MipsDisassembler::decodeRType(u32 encoding, Address address)
{
    Instruction instruction = makeBaseInstruction(encoding, address);
    instruction.type = InstructionType::R_TYPE;
    instruction.rs = extractField(encoding, RS_MASK, 21);
    instruction.rt = extractField(encoding, RT_MASK, 16);
    instruction.rd = extractField(encoding, RD_MASK, 11);
    instruction.shamt = static_cast<u8>((encoding & SHAMT_MASK) >> 6);
    const u32 funct = encoding & FUNCT_MASK;
    if (!decodeWithTable(kRTypeTable, funct, instruction.opcode))
    {
        instruction.opcode = Opcode::UNKNOWN;
        instruction.type = InstructionType::UNKNOWN;
    }

    return instruction;
}

Instruction MipsDisassembler::decodeIType(u32 encoding, Address address)
{
    Instruction instruction = makeBaseInstruction(encoding, address);
    instruction.type = InstructionType::I_TYPE;
    instruction.rs = extractField(encoding, RS_MASK, 21);
    instruction.rt = extractField(encoding, RT_MASK, 16);
    instruction.immediate = static_cast<s16>(encoding & IMM_MASK);

    const u32 opcode = (encoding & OPCODE_MASK) >> 26;

    if (decodeWithTable(kITypeTable, opcode, instruction.opcode))
    {
        return instruction;
    }

    switch (opcode)
    {
    case 0x01:
    {
        const u32 rt = instruction.rt;
        if (decodeWithTable(kRegimmTable, rt, instruction.opcode))
        {
            return instruction;
        }

        if (!decodeWithTable(kRegimmTrapTable, rt, instruction.opcode))
        {
            instruction.opcode = Opcode::UNKNOWN;
            instruction.type = InstructionType::UNKNOWN;
        }
        break;
    }
    default:
        instruction.opcode = Opcode::UNKNOWN;
        instruction.type = InstructionType::UNKNOWN;
        break;
    }

    return instruction;
}

Instruction MipsDisassembler::decodeJType(u32 encoding, Address address)
{
    Instruction instruction = makeBaseInstruction(encoding, address);
    instruction.type = InstructionType::J_TYPE;
    const u32 opcode = (encoding & OPCODE_MASK) >> 26;
    instruction.target = encoding & TARGET_MASK;

    if (opcode == 0x02)
    {
        instruction.opcode = Opcode::J;
    }
    else if (opcode == 0x03)
    {
        instruction.opcode = Opcode::JAL;
    }
    else
    {
        instruction.opcode = Opcode::UNKNOWN;
        instruction.type = InstructionType::UNKNOWN;
    }

    return instruction;
}

Instruction MipsDisassembler::decodeCoprocessor(u32 encoding, Address address)
{
    Instruction instruction = makeBaseInstruction(encoding, address);
    instruction.type = InstructionType::COPROCESSOR;
    instruction.rs = extractField(encoding, RS_MASK, 21);
    instruction.rt = extractField(encoding, RT_MASK, 16);
    instruction.rd = extractField(encoding, RD_MASK, 11);
    instruction.immediate = static_cast<s16>(encoding & IMM_MASK);

    const u32 opcode = (encoding & OPCODE_MASK) >> 26;

    if (opcode == 0x10)
    {
        switch (instruction.rs)
        {
        case 0x00:
            instruction.opcode = Opcode::MFC0;
            break;
        case 0x04:
            instruction.opcode = Opcode::MTC0;
            break;
        case 0x02:
            instruction.opcode = Opcode::CFC0;
            break;
        case 0x06:
            instruction.opcode = Opcode::CTC0;
            break;
        case 0x10:
            switch (encoding & FUNCT_MASK)
            {
            case 0x01:
                instruction.opcode = Opcode::TLBR;
                break;
            case 0x02:
                instruction.opcode = Opcode::TLBWI;
                break;
            case 0x06:
                instruction.opcode = Opcode::TLBWR;
                break;
            case 0x08:
                instruction.opcode = Opcode::TLBP;
                break;
            case 0x10:
                instruction.opcode = Opcode::RFE;
                break;
            default:
                instruction.opcode = Opcode::UNKNOWN;
                instruction.type = InstructionType::UNKNOWN;
                break;
            }
            break;
        case 0x08:
            switch (instruction.rt)
            {
            case 0x00:
                instruction.opcode = Opcode::BC0F;
                break;
            case 0x01:
                instruction.opcode = Opcode::BC0T;
                break;
            default:
                instruction.opcode = Opcode::UNKNOWN;
                instruction.type = InstructionType::UNKNOWN;
                break;
            }
            break;
        default:
            instruction.opcode = Opcode::UNKNOWN;
            instruction.type = InstructionType::UNKNOWN;
            break;
        }

        return instruction;
    }

    if (opcode == 0x12)
    {
        switch (instruction.rs)
        {
        case 0x00:
            instruction.opcode = Opcode::MFC2;
            break;
        case 0x02:
            instruction.opcode = Opcode::CFC2;
            break;
        case 0x04:
            instruction.opcode = Opcode::MTC2;
            break;
        case 0x06:
            instruction.opcode = Opcode::CTC2;
            break;
        case 0x10:
            if (!decodeWithTable(kCop2CommandTable, encoding & FUNCT_MASK, instruction.opcode))
            {
                instruction.opcode = Opcode::UNKNOWN;
                instruction.type = InstructionType::UNKNOWN;
            }
            break;
        default:
            instruction.opcode = Opcode::UNKNOWN;
            instruction.type = InstructionType::UNKNOWN;
            break;
        }
        return instruction;
    }

    if (opcode == 0x32)
    {
        instruction.opcode = Opcode::LWC2;
        return instruction;
    }

    if (opcode == 0x30)
    {
        instruction.opcode = Opcode::LWC0;
        return instruction;
    }

    if (opcode == 0x3A)
    {
        instruction.opcode = Opcode::SWC2;
        return instruction;
    }

    if (opcode == 0x38)
    {
        instruction.opcode = Opcode::SWC0;
        return instruction;
    }

    instruction.opcode = Opcode::UNKNOWN;
    instruction.type = InstructionType::UNKNOWN;
    return instruction;
}

std::vector<Instruction> MipsDisassembler::disassemble(const u8* data, size_t size,
                                                       Address baseAddress)
{
    std::vector<Instruction> result;
    if (!data || size < 4)
    {
        return result;
    }

    const size_t instructionCount = size / 4;
    result.reserve(instructionCount);

    bool nextIsDelaySlot = false;
    std::optional<Address> nextDelaySlotOwner;

    for (size_t i = 0; i < instructionCount; ++i)
    {
        const size_t offset = i * 4;
        const u32 encoding = readLittleEndianU32(data + offset);
        Instruction instruction = decode(encoding, baseAddress + static_cast<Address>(offset));
        instruction.isInDelaySlot = nextIsDelaySlot;
        instruction.delaySlotOwner = nextDelaySlotOwner;

        const bool hasDelaySlot = instruction.isBranch() || instruction.isJump();
        nextIsDelaySlot = hasDelaySlot;
        nextDelaySlotOwner =
            hasDelaySlot ? std::optional<Address>(instruction.address) : std::nullopt;

        result.push_back(instruction);
    }

    return result;
}

std::string MipsDisassembler::getRegisterName(Register reg)
{
    static constexpr std::array<const char*, Registers::NUM_REGISTERS> names = {
        "$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3", "$t0", "$t1", "$t2",
        "$t3",   "$t4", "$t5", "$t6", "$t7", "$s0", "$s1", "$s2", "$s3", "$s4", "$s5",
        "$s6",   "$s7", "$t8", "$t9", "$k0", "$k1", "$gp", "$sp", "$fp", "$ra"};

    if (reg < names.size())
    {
        return names[reg];
    }

    return "$r" + std::to_string(reg);
}

} // namespace disasm
} // namespace psxrecomp
