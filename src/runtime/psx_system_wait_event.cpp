#include "psxrecomp/runtime/psx_system.h"

#include <cstdlib>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{

/// Cycles to advance per busy-wait iteration.
/// Chosen as a small fraction of a frame so hardware events can fire with
/// reasonable granularity while keeping the wait loop inexpensive.
constexpr u32 WAIT_EVENT_TICK_CYCLES = 2048;

/// Maximum iterations before the watchdog fires.
/// With 2048 cycles per tick and ~564480 cycles per frame, this gives
/// roughly 10 frames of emulated CPU time — more than enough for any legitimate
/// PSX BIOS WaitEvent (CD-ROM reads complete in <1 second, VBlank is 1/60s).
constexpr u32 WAIT_EVENT_MAX_ITERATIONS = 10 * (564480 / WAIT_EVENT_TICK_CYCLES);

bool waitEventWatchdogEnabled()
{
    static const bool enabled = []()
    {
        if (const char* env = std::getenv("PSXRECOMP_WAIT_EVENT_WATCHDOG"))
        {
            return env[0] != '0';
        }
        return true; // enabled by default
    }();
    return enabled;
}

} // namespace

bool PsxSystem::waitForEvent(u32 handle)
{
    // Validate the handle and ensure it refers to a NoCallback event.
    const auto* ev = m_events.getEvent(handle);
    if (!ev)
    {
        std::ostringstream msg;
        msg << "WaitEvent: invalid handle 0x" << std::hex << handle;
        m_logger.log(LogLevel::Warn, "bios", msg.str());
        return false;
    }

    // If the event is already delivered, return immediately.
    if (m_events.isEventDelivered(handle))
    {
        m_logger.log(LogLevel::Debug, "bios", "WaitEvent: already delivered");
        return true;
    }

    // If the event is not enabled, it can never be delivered — watchdog.
    if (ev->status != EventStatus::Enabled)
    {
        std::ostringstream msg;
        msg << "WaitEvent: event 0x" << std::hex << handle
            << " is not enabled (status=" << static_cast<u32>(ev->status) << ") — cannot block";
        m_logger.log(LogLevel::Warn, "bios", msg.str());
        return false;
    }

    {
        std::ostringstream msg;
        msg << "WaitEvent: blocking on handle 0x" << std::hex << handle << " class=0x"
            << ev->classId << " spec=0x" << ev->spec;
        m_logger.log(LogLevel::Debug, "bios", msg.str());
    }

    const bool watchdogActive = waitEventWatchdogEnabled();
    const u32 maxIterations = watchdogActive ? WAIT_EVENT_MAX_ITERATIONS : 0xFFFFFFFFu;

    for (u32 iteration = 0; iteration < maxIterations; ++iteration)
    {
        // Advance hardware: timers, CD-ROM, SPU, scheduler, VBlank.
        tickCpuCycles(WAIT_EVENT_TICK_CYCLES);

        // Pump interrupt delivery so IRQ handlers can deliver the event.
        serviceInterrupts();

        // Check if delivery happened.
        if (m_events.isEventDelivered(handle))
        {
            {
                std::ostringstream msg;
                msg << "WaitEvent: delivered after " << std::dec << (iteration + 1) << " ticks";
                m_logger.log(LogLevel::Debug, "bios", msg.str());
            }
            return true;
        }

        // Re-validate: event may have been closed or disabled externally.
        const auto* current = m_events.getEvent(handle);
        if (!current || current->status == EventStatus::Free ||
            current->status == EventStatus::Disabled)
        {
            std::ostringstream msg;
            msg << "WaitEvent: event 0x" << std::hex << handle
                << " became invalid during wait — aborting";
            m_logger.log(LogLevel::Warn, "bios", msg.str());
            return false;
        }
    }

    // Watchdog fired.
    {
        std::ostringstream msg;
        msg << "WaitEvent: watchdog fired after " << std::dec << maxIterations
            << " iterations on handle 0x" << std::hex << handle << " class=0x" << ev->classId
            << " spec=0x" << ev->spec << " — possible impossible wait";
        m_logger.log(LogLevel::Error, "bios", msg.str());
    }
    return false;
}

} // namespace runtime
} // namespace psxrecomp
