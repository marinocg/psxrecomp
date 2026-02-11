#pragma once

#include "psxrecomp/runtime/interrupt_controller.h"
#include "psxrecomp/types.h"

#include <array>
#include <functional>

namespace psxrecomp
{
namespace runtime
{

class TimerController
{
  public:
    using InterruptCallback = std::function<void(InterruptLine)>;

    void reset();

    u16 readCounter(size_t index) const;
    u16 readMode(size_t index);
    u16 readTarget(size_t index) const;

    void writeCounter(size_t index, u16 value);
    void writeMode(size_t index, u16 value);
    void writeTarget(size_t index, u16 value);

    void tick(u32 cpuCycles, const InterruptCallback& onInterrupt);

  private:
    struct Channel
    {
        u16 counter = 0;
        u16 mode = 0;
        u16 target = 0;
        u32 cycleCarry = 0;
        bool targetReached = false;
        bool overflowReached = false;
    };

    std::array<Channel, 3> m_channels = {};

    static bool isValidIndex(size_t index);
    static u32 dividerForChannel(size_t index, const Channel& channel);
    static InterruptLine interruptLineForTimer(size_t index);
};

} // namespace runtime
} // namespace psxrecomp
