#include "psxrecomp/runtime/input.h"

namespace psxrecomp
{
namespace runtime
{

void InputController::reset()
{
    m_state = 0xFFFF;
}

void InputController::setButton(ControllerButton button, bool pressed)
{
    if (pressed)
    {
        m_state &= ~static_cast<u16>(button);
    }
    else
    {
        m_state |= static_cast<u16>(button);
    }
}

u16 InputController::readState() const
{
    return m_state;
}

} // namespace runtime
} // namespace psxrecomp
