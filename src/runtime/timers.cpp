#include "psxrecomp/runtime/timers.h"

#include <algorithm>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u16 MODE_RESET_ON_TARGET = 1u << 3;
constexpr u16 MODE_IRQ_ON_TARGET = 1u << 4;
constexpr u16 MODE_IRQ_ON_OVERFLOW = 1u << 5;
constexpr u16 MODE_TARGET_REACHED_FLAG = 1u << 11;
constexpr u16 MODE_OVERFLOW_REACHED_FLAG = 1u << 12;
} // namespace

void TimerController::reset()
{
    for (auto& channel : m_channels)
    {
        channel = {};
    }
}

u16 TimerController::readCounter(size_t index) const
{
    if (!isValidIndex(index))
    {
        return 0;
    }
    return m_channels[index].counter;
}

u16 TimerController::readMode(size_t index)
{
    if (!isValidIndex(index))
    {
        return 0;
    }

    Channel& channel = m_channels[index];
    u16 value = channel.mode;
    if (channel.targetReached)
    {
        value |= MODE_TARGET_REACHED_FLAG;
    }
    if (channel.overflowReached)
    {
        value |= MODE_OVERFLOW_REACHED_FLAG;
    }

    channel.targetReached = false;
    channel.overflowReached = false;
    return value;
}

u16 TimerController::readTarget(size_t index) const
{
    if (!isValidIndex(index))
    {
        return 0;
    }
    return m_channels[index].target;
}

void TimerController::writeCounter(size_t index, u16 value)
{
    if (!isValidIndex(index))
    {
        return;
    }
    m_channels[index].counter = value;
}

void TimerController::writeMode(size_t index, u16 value)
{
    if (!isValidIndex(index))
    {
        return;
    }
    Channel& channel = m_channels[index];
    channel.mode = value;
    channel.counter = 0;
    channel.cycleCarry = 0;
    channel.targetReached = false;
    channel.overflowReached = false;
}

void TimerController::writeTarget(size_t index, u16 value)
{
    if (!isValidIndex(index))
    {
        return;
    }
    m_channels[index].target = value;
}

void TimerController::tick(u32 cpuCycles, const InterruptCallback& onInterrupt)
{
    for (size_t index = 0; index < m_channels.size(); ++index)
    {
        Channel& channel = m_channels[index];
        const u32 divider = dividerForChannel(index, channel);

        const uint64_t totalCycles = static_cast<uint64_t>(channel.cycleCarry) + cpuCycles;
        const u32 steps = static_cast<u32>(totalCycles / divider);
        channel.cycleCarry = static_cast<u32>(totalCycles % divider);

        for (u32 i = 0; i < steps; ++i)
        {
            ++channel.counter;

            if (channel.counter == channel.target)
            {
                channel.targetReached = true;
                if ((channel.mode & MODE_IRQ_ON_TARGET) != 0 && onInterrupt)
                {
                    onInterrupt(interruptLineForTimer(index));
                }
                if ((channel.mode & MODE_RESET_ON_TARGET) != 0)
                {
                    channel.counter = 0;
                    continue;
                }
            }

            if (channel.counter == 0)
            {
                channel.overflowReached = true;
                if ((channel.mode & MODE_IRQ_ON_OVERFLOW) != 0 && onInterrupt)
                {
                    onInterrupt(interruptLineForTimer(index));
                }
            }
        }
    }
}

bool TimerController::isValidIndex(size_t index)
{
    return index < 3;
}

u32 TimerController::dividerForChannel(size_t index, const Channel& channel)
{
    if (index == 2)
    {
        const u16 clockSelect = static_cast<u16>((channel.mode >> 8) & 0x3);
        if (clockSelect == 0x2)
        {
            return 8;
        }
    }
    return 1;
}

InterruptLine TimerController::interruptLineForTimer(size_t index)
{
    switch (index)
    {
    case 0:
        return InterruptLine::Timer0;
    case 1:
        return InterruptLine::Timer1;
    default:
        return InterruptLine::Timer2;
    }
}

} // namespace runtime
} // namespace psxrecomp
