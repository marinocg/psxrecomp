#pragma once

#include "psxrecomp/types.h"

namespace psxrecomp
{
namespace runtime
{

enum class InterruptLine : u16
{
    VBlank = 1 << 0,
    Gpu = 1 << 1,
    Cdrom = 1 << 2,
    Dma = 1 << 3,
    Timer0 = 1 << 4,
    Timer1 = 1 << 5,
    Timer2 = 1 << 6,
    Controller = 1 << 7,
    Sio = 1 << 8,
    Spu = 1 << 9,
    Pio = 1 << 10
};

class InterruptController
{
  public:
    void reset();

    u32 readStatus() const;
    u32 readMask() const;
    void writeStatus(u32 value);
    void writeMask(u32 value);

    void raise(InterruptLine line);

    bool isInterruptPending() const;

  private:
    u32 m_status = 0;
    u32 m_mask = 0;
};

} // namespace runtime
} // namespace psxrecomp
