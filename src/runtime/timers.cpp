#include "psxrecomp/runtime/timers.h"

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u16 MODE_CLOCK_SOURCE_MASK = 0x3u << 8;
constexpr u16 MODE_RESET_ON_TARGET = 1u << 3;
constexpr u16 MODE_IRQ_ON_TARGET = 1u << 4;
constexpr u16 MODE_IRQ_ON_OVERFLOW = 1u << 5;
constexpr u16 MODE_TARGET_REACHED_FLAG = 1u << 11;
constexpr u16 MODE_OVERFLOW_REACHED_FLAG = 1u << 12;

bool didCounterHitTarget(u16 counter, u16 target, u32 steps)
{
    if (steps == 0)
    {
        return false;
    }

    u32 distance = (static_cast<u32>(target) - static_cast<u32>(counter)) & 0xFFFFu;
    if (distance == 0)
    {
        distance = 0x10000u;
    }
    return steps >= distance;
}

void raiseTimerInterrupt(const TimerController::InterruptCallback& onInterrupt, InterruptLine line,
                         bool enabled)
{
    if (enabled && onInterrupt)
    {
        onInterrupt(line);
    }
}

} // namespace

void TimerController::advanceChannel(Channel& channel, size_t index, u32 steps,
                                     const InterruptCallback& onInterrupt)
{
    if (steps == 0)
    {
        return;
    }

    const bool resetOnTarget = (channel.mode & MODE_RESET_ON_TARGET) != 0;
    const bool irqOnTarget = (channel.mode & MODE_IRQ_ON_TARGET) != 0;
    const bool irqOnOverflow = (channel.mode & MODE_IRQ_ON_OVERFLOW) != 0;
    const InterruptLine line = TimerController::interruptLineForTimer(index);

    if (resetOnTarget)
    {
        const u32 counter = channel.counter;
        const u32 target = channel.target;
        const u32 period = (target == 0) ? 0x10000u : target;

        u32 firstEventSteps = 0;
        if (target == 0)
        {
            firstEventSteps = 0x10000u - counter;
        }
        else if (counter < target)
        {
            firstEventSteps = target - counter;
        }
        else
        {
            firstEventSteps = (0x10000u - counter) + target;
        }

        if (steps < firstEventSteps)
        {
            channel.counter = static_cast<u16>((counter + steps) & 0xFFFFu);
            return;
        }

        const u32 remainingAfterFirstEvent = steps - firstEventSteps;
        const u32 targetEvents = 1 + (remainingAfterFirstEvent / period);
        channel.counter = static_cast<u16>(remainingAfterFirstEvent % period);
        if (targetEvents > 0)
        {
            channel.targetReached = true;
            raiseTimerInterrupt(onInterrupt, line, irqOnTarget);
        }
        return;
    }

    const u32 total = static_cast<u32>(channel.counter) + steps;
    const u16 newCounter = static_cast<u16>(total & 0xFFFF);
    const u32 overflowEvents = total >> 16;

    const bool targetReached = didCounterHitTarget(channel.counter, channel.target, steps);

    if (targetReached)
    {
        channel.targetReached = true;
        raiseTimerInterrupt(onInterrupt, line, irqOnTarget);
    }

    if (overflowEvents > 0)
    {
        channel.overflowReached = true;
        raiseTimerInterrupt(onInterrupt, line, irqOnOverflow);
    }

    channel.counter = newCounter;
}

void TimerController::tickChannel(Channel& channel, size_t index, u32 cpuCycles,
                                  const InterruptCallback& onInterrupt)
{
    const u32 divider = TimerController::dividerForChannel(index, channel);
    const uint64_t totalCycles = static_cast<uint64_t>(channel.cycleCarry) + cpuCycles;
    const u32 steps = static_cast<u32>(totalCycles / divider);
    channel.cycleCarry = static_cast<u32>(totalCycles % divider);
    advanceChannel(channel, index, steps, onInterrupt);
}

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
        if (usesDisplayLineClock(index, m_channels[index]))
        {
            continue;
        }
        TimerController::tickChannel(m_channels[index], index, cpuCycles, onInterrupt);
    }
}

void TimerController::tickDisplayLine(const InterruptCallback& onInterrupt)
{
    for (size_t index = 0; index < m_channels.size(); ++index)
    {
        if (!usesDisplayLineClock(index, m_channels[index]))
        {
            continue;
        }
        advanceChannel(m_channels[index], index, 1, onInterrupt);
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

bool TimerController::usesDisplayLineClock(size_t index, const Channel& channel)
{
    if (index != 1)
    {
        return false;
    }

    const u16 clockSelect = static_cast<u16>((channel.mode & MODE_CLOCK_SOURCE_MASK) >> 8);
    return clockSelect == 0x1u || clockSelect == 0x3u;
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
