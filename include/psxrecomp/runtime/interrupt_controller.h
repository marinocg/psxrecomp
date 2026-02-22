#pragma once

#include "psxrecomp/types.h"

#include <functional>

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
    struct TraceEvent
    {
        enum class Kind
        {
            Reset,
            WriteStatus,
            WriteMask,
            Raise,
            RestoreState
        };

        Kind kind = Kind::Reset;
        u32 value = 0; ///< Raw write value or raised line bit.
        u32 statusBefore = 0;
        u32 statusAfter = 0;
        u32 maskBefore = 0;
        u32 maskAfter = 0;
    };

    using TraceHook = std::function<void(const TraceEvent&)>;

    void reset();

    u32 readStatus() const;
    u32 readMask() const;
    void writeStatus(u32 value);
    void writeMask(u32 value);
    void setTraceHook(TraceHook hook);

    void raise(InterruptLine line);
    void restoreState(u32 status, u32 mask);

    bool isInterruptPending() const;

  private:
    void emitTrace(TraceEvent::Kind kind, u32 value, u32 statusBefore, u32 statusAfter,
                   u32 maskBefore, u32 maskAfter);

    u32 m_status = 0;
    u32 m_mask = 0;
    TraceHook m_traceHook;
};

} // namespace runtime
} // namespace psxrecomp
