#include "psxrecomp/runtime/psx_system.h"

#include "bios_helpers.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

bool PsxSystem::callBiosVectorC0(u32 functionId, u32* regs)
{
    (void)regs;

    switch (functionId)
    {
    case 0x00: // EnqueueTimerAndVblankIrqs
    {
        // On real hardware this installs default interrupt handlers for
        // VBlank and timer IRQs. In our runtime the interrupt dispatcher
        // handles delivery directly, so we just ensure the IRQ mask has
        // VBlank and timer bits enabled.
        const u32 currentMask = m_interrupts.readMask();
        const u32 timerVblankBits =
            static_cast<u32>(InterruptLine::VBlank) | static_cast<u32>(InterruptLine::Timer0) |
            static_cast<u32>(InterruptLine::Timer1) | static_cast<u32>(InterruptLine::Timer2);
        m_interrupts.writeMask(currentMask | timerVblankBits);
        m_logger.log(LogLevel::Debug, "bios", "EnqueueTimerAndVblankIrqs");
        return true;
    }
    case 0x01: // EnqueueSyscallHandler
        m_logger.log(LogLevel::Debug, "bios", "EnqueueSyscallHandler (stub)");
        return true;
    case 0x02: // SysEnqIntRP
    {
        // Priority chain enqueue — not needed for event-driven model.
        m_logger.log(LogLevel::Debug, "bios", "SysEnqIntRP (stub)");
        return true;
    }
    case 0x03: // SysDeqIntRP
    {
        m_logger.log(LogLevel::Debug, "bios", "SysDeqIntRP (stub)");
        return true;
    }
    case 0x07: // InstallExceptionHandlers
    {
        m_logger.log(LogLevel::Debug, "bios", "InstallExceptionHandlers (acknowledged)");
        return true;
    }
    case 0x08: // SysInitMemory
        m_logger.log(LogLevel::Debug, "bios", "SysInitMemory (stub)");
        return true;
    case 0x09: // SysInitKernelVariables
        m_logger.log(LogLevel::Debug, "bios", "SysInitKernelVariables (stub)");
        return true;
    case 0x0A: // ChangeClearRCnt
    {
        // Changes automatic acknowledge behavior for timer interrupts.
        // Our dispatcher handles ack, so this is a no-op.
        m_logger.log(LogLevel::Debug, "bios", "ChangeClearRCnt (stub)");
        return true;
    }
    case 0x0C: // InitDefInt
        m_logger.log(LogLevel::Debug, "bios", "InitDefInt (stub)");
        return true;
    case 0x12: // InstallDevices
        m_logger.log(LogLevel::Debug, "bios", "InstallDevices (stub)");
        return true;
    case 0x1C: // AdjustA0Table
        m_logger.log(LogLevel::Debug, "bios", "AdjustA0Table (stub)");
        return true;
    default:
        return false;
    }
}

} // namespace runtime
} // namespace psxrecomp
