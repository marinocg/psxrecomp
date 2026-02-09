#include "psxrecomp/disasm/instruction.h"

namespace psxrecomp
{
namespace disasm
{

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
    case Opcode::BC0F:
    case Opcode::BC0T:
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

bool Instruction::hasDelaySlot() const
{
    return isBranch() || isJump();
}

bool Instruction::isReturn() const
{
    return opcode == Opcode::JR && rs == Registers::RA;
}

std::optional<Address> Instruction::getBranchTarget() const
{
    if (!isBranch())
    {
        return std::nullopt;
    }

    const s32 offset = static_cast<s32>(immediate) << 2;
    return static_cast<Address>(address + 4 + offset);
}

std::optional<Address> Instruction::getJumpTarget() const
{
    if (opcode == Opcode::J || opcode == Opcode::JAL)
    {
        const Address base = (address + 4) & 0xF0000000u;
        return base | (target << 2);
    }

    return std::nullopt;
}

std::optional<Address> Instruction::getTargetAddress() const
{
    if (auto branchTarget = getBranchTarget())
    {
        return branchTarget;
    }

    return getJumpTarget();
}

MemoryAccessType Instruction::getMemoryAccessType() const
{
    switch (opcode)
    {
    case Opcode::LB:
    case Opcode::LH:
    case Opcode::LW:
    case Opcode::LBU:
    case Opcode::LHU:
    case Opcode::LWL:
    case Opcode::LWR:
    case Opcode::LWC0:
    case Opcode::LWC2:
        return MemoryAccessType::LOAD;
    case Opcode::SB:
    case Opcode::SH:
    case Opcode::SW:
    case Opcode::SWL:
    case Opcode::SWR:
    case Opcode::SWC0:
    case Opcode::SWC2:
        return MemoryAccessType::STORE;
    default:
        return MemoryAccessType::NONE;
    }
}

MemoryAccessSize Instruction::getMemoryAccessSize() const
{
    switch (opcode)
    {
    case Opcode::LB:
    case Opcode::LBU:
    case Opcode::SB:
        return MemoryAccessSize::BYTE;
    case Opcode::LH:
    case Opcode::LHU:
    case Opcode::SH:
        return MemoryAccessSize::HALF_WORD;
    case Opcode::LW:
    case Opcode::LWL:
    case Opcode::LWR:
    case Opcode::SW:
    case Opcode::SWL:
    case Opcode::SWR:
    case Opcode::LWC0:
    case Opcode::LWC2:
    case Opcode::SWC0:
    case Opcode::SWC2:
        return MemoryAccessSize::WORD;
    default:
        return MemoryAccessSize::UNKNOWN;
    }
}

AddressingMode Instruction::getAddressingMode() const
{
    if (isBranch())
    {
        return AddressingMode::PC_RELATIVE;
    }

    switch (opcode)
    {
    case Opcode::J:
    case Opcode::JAL:
        return AddressingMode::ABSOLUTE;
    case Opcode::JR:
    case Opcode::JALR:
        return AddressingMode::REGISTER;
    case Opcode::LB:
    case Opcode::LH:
    case Opcode::LW:
    case Opcode::LBU:
    case Opcode::LHU:
    case Opcode::LWL:
    case Opcode::LWR:
    case Opcode::SB:
    case Opcode::SH:
    case Opcode::SW:
    case Opcode::SWL:
    case Opcode::SWR:
    case Opcode::CACHE:
    case Opcode::LWC0:
    case Opcode::SWC0:
    case Opcode::LWC2:
    case Opcode::SWC2:
        return AddressingMode::BASE_OFFSET;
    default:
        return AddressingMode::NONE;
    }
}

} // namespace disasm
} // namespace psxrecomp
