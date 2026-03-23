#include "psxrecomp/runtime/cop0.h"

namespace psxrecomp
{
namespace runtime
{

void Cop0::reset()
{
    m_registers.fill(0);
}

void Cop0::applyBootState(const CpuBootState& state)
{
    restoreState(state.badVaddr, state.status, state.cause, state.epc);
}

u32 Cop0::mfc0(u8 rd) const
{
    switch (rd)
    {
    case RegisterIndex::BadVAddr:
    case RegisterIndex::Status:
    case RegisterIndex::Cause:
    case RegisterIndex::Epc:
        return m_registers[rd];
    case RegisterIndex::PrId:
        // PSX CXD8606 Processor ID as documented by PSX-SPX.
        return 0x00000002u;
    default:
        return 0;
    }
}

void Cop0::mtc0(u8 rd, u32 value)
{
    switch (rd)
    {
    case RegisterIndex::BadVAddr:
    case RegisterIndex::Status:
    case RegisterIndex::Epc:
        m_registers[rd] = value;
        break;
    case RegisterIndex::Cause:
        // MTC0 Cause only exposes software IP bits (IP0/IP1); keep
        // hardware-pending and exception/BD state untouched.
        m_registers[RegisterIndex::Cause] =
            (m_registers[RegisterIndex::Cause] & ~CauseSoftwareInterruptPendingMask) |
            (value & CauseSoftwareInterruptPendingMask);
        break;
    default:
        break;
    }
}

bool Cop0::cop2Enabled() const
{
    return (m_registers[RegisterIndex::Status] & StatusCop2EnableBit) != 0u;
}

void Cop0::noteInterruptControllerPending(bool pending)
{
    if (pending)
    {
        m_registers[RegisterIndex::Cause] |= CauseIrqControllerPendingBit;
    }
    else
    {
        m_registers[RegisterIndex::Cause] &= ~CauseIrqControllerPendingBit;
    }
}

bool Cop0::irqEnableHw0() const
{
    const u32 status = m_registers[RegisterIndex::Status];
    return (status & StatusCurrentInterruptEnableBit) != 0u &&
           (status & StatusInterruptMaskIp2Bit) != 0u;
}

bool Cop0::shouldTakeInterruptException() const
{
    if (isInExceptionMode())
    {
        return false;
    }

    const u32 status = m_registers[RegisterIndex::Status];
    if ((status & StatusCurrentInterruptEnableBit) == 0u)
    {
        return false;
    }

    const u32 cause = m_registers[RegisterIndex::Cause];
    const u32 pendingEnabled =
        (cause & CauseInterruptPendingIp0Ip2Mask) & (status & StatusInterruptMaskIp0Ip2Bits);
    return pendingEnabled != 0u;
}

bool Cop0::isInExceptionMode() const
{
    const u32 statusModeBits = m_registers[RegisterIndex::Status] & StatusModeBitsMask;
    return (statusModeBits & StatusCurrentModeMask) == 0u && statusModeBits != 0u;
}

void Cop0::restoreState(u32 badVaddr, u32 status, u32 cause, u32 epc)
{
    m_registers[RegisterIndex::BadVAddr] = badVaddr;
    m_registers[RegisterIndex::Status] = status;
    m_registers[RegisterIndex::Cause] = cause;
    m_registers[RegisterIndex::Epc] = epc;
}

void Cop0::exceptionEnter(ExceptionCode code, u32 pc, bool inDelaySlot, std::optional<u32> badVaddr)
{
    const u32 status = m_registers[RegisterIndex::Status];
    u32 pushedMode = ((status & StatusModeBitsMask) << 2) & StatusModeBitsMask;
    // Enter kernel mode with interrupts disabled (KUc/IEc cleared).
    pushedMode &= ~0x3u;
    m_registers[RegisterIndex::Status] = (status & ~StatusModeBitsMask) | pushedMode;

    u32 cause = m_registers[RegisterIndex::Cause];
    cause &= ~(CauseExcCodeMask | CauseBranchDelayBit);
    cause |= (static_cast<u32>(code) & 0x1Fu) << 2;
    if (inDelaySlot)
    {
        cause |= CauseBranchDelayBit;
    }
    m_registers[RegisterIndex::Cause] = cause;

    m_registers[RegisterIndex::Epc] = inDelaySlot ? (pc - 4u) : pc;
    if (badVaddr.has_value())
    {
        m_registers[RegisterIndex::BadVAddr] = *badVaddr;
    }
}

void Cop0::rfe()
{
    const u32 status = m_registers[RegisterIndex::Status];
    m_registers[RegisterIndex::Status] =
        (status & ~StatusModeBitsMask) | ((status & StatusModeBitsMask) >> 2);
}

} // namespace runtime
} // namespace psxrecomp
