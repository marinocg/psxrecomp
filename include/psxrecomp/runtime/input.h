#pragma once

#include "psxrecomp/types.h"

namespace psxrecomp
{
namespace runtime
{

enum class ControllerButton : u16
{
    Select = 1 << 0,
    L3 = 1 << 1,
    R3 = 1 << 2,
    Start = 1 << 3,
    Up = 1 << 4,
    Right = 1 << 5,
    Down = 1 << 6,
    Left = 1 << 7,
    L2 = 1 << 8,
    R2 = 1 << 9,
    L1 = 1 << 10,
    R1 = 1 << 11,
    Triangle = 1 << 12,
    Circle = 1 << 13,
    Cross = 1 << 14,
    Square = 1 << 15
};

class InputController
{
  public:
    void reset();

    void setButton(ControllerButton button, bool pressed);
    u16 readState() const;

  private:
    u16 m_state = 0xFFFF;
};

} // namespace runtime
} // namespace psxrecomp
