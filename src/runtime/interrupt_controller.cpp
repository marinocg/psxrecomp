#include "psxrecomp/runtime/interrupt_controller.h"

#include <utility>

namespace psxrecomp
{
namespace runtime
{

void InterruptController::reset()
{
    const u32 statusBefore = m_status;
    const u32 maskBefore = m_mask;
    m_status = 0;
    m_mask = 0;
    emitTrace(TraceEvent::Kind::Reset, 0, statusBefore, m_status, maskBefore, m_mask);
}

u32 InterruptController::readStatus() const
{
    return m_status;
}

u32 InterruptController::readMask() const
{
    return m_mask;
}

void InterruptController::writeStatus(u32 value)
{
    const u32 statusBefore = m_status;
    const u32 maskBefore = m_mask;
    // PSX I_STAT acknowledge semantics: bits written as 0 are cleared,
    // bits written as 1 are preserved.
    m_status &= (value & ValidLineMask);
    emitTrace(TraceEvent::Kind::WriteStatus, value, statusBefore, m_status, maskBefore, m_mask);
}

void InterruptController::writeMask(u32 value)
{
    const u32 statusBefore = m_status;
    const u32 maskBefore = m_mask;
    m_mask = value & ValidLineMask;
    emitTrace(TraceEvent::Kind::WriteMask, value, statusBefore, m_status, maskBefore, m_mask);
}

void InterruptController::setTraceHook(TraceHook hook)
{
    m_traceHook = std::move(hook);
}

void InterruptController::raise(InterruptLine line)
{
    const u32 statusBefore = m_status;
    const u32 maskBefore = m_mask;
    m_status |= (static_cast<u32>(line) & ValidLineMask);
    emitTrace(TraceEvent::Kind::Raise, static_cast<u32>(line), statusBefore, m_status, maskBefore,
              m_mask);
}

void InterruptController::restoreState(u32 status, u32 mask)
{
    const u32 statusBefore = m_status;
    const u32 maskBefore = m_mask;
    m_status = status & ValidLineMask;
    m_mask = mask & ValidLineMask;
    emitTrace(TraceEvent::Kind::RestoreState, 0, statusBefore, m_status, maskBefore, m_mask);
}

bool InterruptController::isInterruptPending() const
{
    return (m_status & m_mask) != 0;
}

void InterruptController::emitTrace(TraceEvent::Kind kind, u32 value, u32 statusBefore,
                                    u32 statusAfter, u32 maskBefore, u32 maskAfter)
{
    if (!m_traceHook)
    {
        return;
    }

    TraceEvent event;
    event.kind = kind;
    event.value = value;
    event.statusBefore = statusBefore;
    event.statusAfter = statusAfter;
    event.maskBefore = maskBefore;
    event.maskAfter = maskAfter;
    m_traceHook(event);
}

} // namespace runtime
} // namespace psxrecomp
