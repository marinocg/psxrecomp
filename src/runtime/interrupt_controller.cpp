#include "psxrecomp/runtime/interrupt_controller.h"

namespace psxrecomp
{
namespace runtime
{

void InterruptController::reset()
{
    m_status = 0;
    m_mask = 0;
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
    m_status &= ~value;
}

void InterruptController::writeMask(u32 value)
{
    m_mask = value;
}

void InterruptController::raise(InterruptLine line)
{
    m_status |= static_cast<u32>(line);
}

void InterruptController::restoreState(u32 status, u32 mask)
{
    m_status = status;
    m_mask = mask;
}
bool InterruptController::isInterruptPending() const
{
    return (m_status & m_mask) != 0;
}

} // namespace runtime
} // namespace psxrecomp
