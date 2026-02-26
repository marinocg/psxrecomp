#include "psxrecomp/runtime/cop0.h"

namespace psxrecomp
{
namespace runtime
{

void Cop0::reset()
{
    m_registers.fill(0);
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
    case RegisterIndex::Cause:
    case RegisterIndex::Epc:
        m_registers[rd] = value;
        break;
    default:
        break;
    }
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
