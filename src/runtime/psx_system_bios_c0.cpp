#include "psxrecomp/runtime/psx_system.h"

#include "bios_helpers.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

bool PsxSystem::callBiosVectorC0(u32 functionId, u32* regs)
{
    const u32 a0 = regs[4];
    const u32 a1 = regs[5];

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
        syncCop0InterruptPending();
        m_logger.log(LogLevel::Debug, "bios", "EnqueueTimerAndVblankIrqs");
        return true;
    }
    case 0x01: // EnqueueSyscallHandler
        m_logger.log(LogLevel::Debug, "bios", "EnqueueSyscallHandler (stub)");
        return true;
    case 0x02: // SysEnqIntRP
    {
        // PSX-SPX: C(02h) SysEnqIntRP(priority, struc)
        // Inserts a new element at the head of the specified priority chain.
        // The BIOS writes the "next" pointer into struc+0.
        //
        // Structure layout (16 bytes):
        //   +00 next pointer (written by BIOS)
        //   +04 func2 pointer (optional)
        //   +08 func1 pointer
        //   +0C unused
        if (a0 < m_irqChainHeads.size() && a1 != 0)
        {
            const u32 previousHead = m_irqChainHeads[a0];
            write<u32>(a1 + 0x00, previousHead);
            m_irqChainHeads[a0] = a1;
            regs[2] = 1;

            // Snapshot the data region around the chain struct so that
            // MMIO pointers (0x1F80xxxx) used by the handlers can be
            // restored if a buffer overrun zeroes them later.
            saveIrqChainSnapshot(a0, a1);
        }
        else
        {
            regs[2] = 0;
        }
        m_logger.log(LogLevel::Debug, "bios", "SysEnqIntRP");
        return true;
    }
    case 0x03: // SysDeqIntRP
    {
        // PSX-SPX: C(03h) SysDeqIntRP(priority, struc)
        // Removes the specified element from the chain; returns r2=struc or 0.
        if (a0 >= m_irqChainHeads.size() || a1 == 0)
        {
            regs[2] = 0;
            return true;
        }

        u32* head = &m_irqChainHeads[a0];
        u32 node = *head;
        u32 prev = 0;
        constexpr int kMaxNodes = 64;
        for (int safety = 0; node != 0 && safety < kMaxNodes; ++safety)
        {
            if (node == a1)
            {
                const u32 next = read<u32>(node + 0x00);
                if (prev == 0)
                {
                    *head = next;
                }
                else
                {
                    write<u32>(prev + 0x00, next);
                }
                regs[2] = a1;
                m_logger.log(LogLevel::Debug, "bios", "SysDeqIntRP");
                return true;
            }
            prev = node;
            node = read<u32>(node + 0x00);
        }

        regs[2] = 0;
        m_logger.log(LogLevel::Debug, "bios", "SysDeqIntRP (not found)");
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
        // PSX-SPX: C(0Ah) ChangeClearRCnt(t, flag)
        // t: 0=Timer0, 1=Timer1, 2=Timer2, 3=VBlank
        // flag: 0=normal handler, 1=auto-ack and immediately return from exception
        // Returns old flag value in v0.
        if (a0 < m_changeClearRCntPolicy.size())
        {
            const bool oldFlag = m_changeClearRCntPolicy[a0];
            m_changeClearRCntPolicy[a0] = (a1 != 0);
            regs[2] = oldFlag ? 1u : 0u;

            std::ostringstream msg;
            msg << "ChangeClearRCnt t=" << std::dec << a0 << " flag=" << a1
                << " old=" << (oldFlag ? 1 : 0);
            m_logger.log(LogLevel::Debug, "bios", msg.str());
        }
        else
        {
            regs[2] = 0;
        }
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
