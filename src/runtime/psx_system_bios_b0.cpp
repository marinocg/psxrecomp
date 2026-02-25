#include "psxrecomp/runtime/psx_system.h"

#include "bios_helpers.h"

#include <cstdlib>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
bool traceIrqFlowEnabled()
{
    if (const char* env = std::getenv("PSXRECOMP_TRACE_IRQ_FLOW"))
    {
        return env[0] == '1';
    }
    return false;
}
} // namespace

bool PsxSystem::callBiosVectorB0(u32 functionId, u32* regs)
{
    const u32 a0 = regs[4];
    const u32 a1 = regs[5];
    const u32 a2 = regs[6];
    const u32 a3 = regs[7];

    switch (functionId)
    {
    case 0x00: // alloc_kernel_memory
    {
        static u32 kernelHeap = 0x80010000u;
        u32 size = (a0 + 7) & ~7u;
        regs[2] = kernelHeap;
        kernelHeap += size;
        return true;
    }
    case 0x07: // DeliverEvent(class, spec)
    {
        m_logger.log(LogLevel::Debug, "bios", "DeliverEvent");
        // Deliver to all matching events.
        auto callbacks = m_events.deliverByClassSpec(a0, static_cast<u16>(a1 & 0xFFFF));
        for (u32 addr : callbacks)
        {
            invokeCallback(addr);
        }
        return true;
    }
    case 0x08: // OpenEvent(class, spec, mode, callback)
    {
        auto mode = static_cast<EventMode>(a2);
        u32 handle = m_events.openEvent(a0, static_cast<u16>(a1 & 0xFFFF), mode, a3);
        regs[2] = handle;
        {
            std::ostringstream msg;
            msg << "OpenEvent class=0x" << std::hex << a0 << " spec=0x" << a1 << " mode=0x" << a2
                << " cb=0x" << a3 << " => handle=0x" << handle;
            m_logger.log(LogLevel::Debug, "bios", msg.str());
        }
        return true;
    }
    case 0x09: // CloseEvent(handle)
    {
        m_events.closeEvent(a0);
        m_logger.log(LogLevel::Debug, "bios", "CloseEvent");
        return true;
    }
    case 0x0A: // WaitEvent(handle)
    {
        // Real BIOS blocks until delivery. In this runtime we keep
        // WaitEvent non-blocking and do not fabricate delivery.
        // The game can observe delivery via TestEvent after IRQ dispatch.
        m_logger.log(LogLevel::Debug, "bios", "WaitEvent (non-blocking stub)");
        return true;
    }
    case 0x0B: // TestEvent(handle)
    {
        regs[2] = m_events.testEvent(a0);
        return true;
    }
    case 0x0C: // EnableEvent(handle)
    {
        m_events.enableEvent(a0);
        regs[2] = 1;
        m_logger.log(LogLevel::Debug, "bios", "EnableEvent");
        return true;
    }
    case 0x0D: // DisableEvent(handle)
    {
        m_events.disableEvent(a0);
        regs[2] = 1;
        m_logger.log(LogLevel::Debug, "bios", "DisableEvent");
        return true;
    }
    case 0x12: // InitPad
        return true;
    case 0x13: // StartPad
        return true;
    case 0x17: // ReturnFromException
        // In callback/IRQ context this exits the current callback and returns
        // to the interrupted execution point.
        if (m_inCallbackInvocation)
        {
            if (traceIrqFlowEnabled())
            {
                std::ostringstream msg;
                msg << "event=return_from_exception source=bios_b0_17 action=throw pc=0x"
                    << std::hex << m_debugOverlay.lastProgramCounter();
                m_logger.log(LogLevel::Info, "irq_trace", msg.str());
            }
            throw ReturnFromExceptionSignal{};
        }
        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=return_from_exception source=bios_b0_17 action=ignored pc=0x" << std::hex
                << m_debugOverlay.lastProgramCounter();
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        return true;
    case 0x18: // ResetEntryInt
        regs[2] = m_hookEntryInt.descriptorAddress;
        m_hookEntryInt = {};
        return true;
    case 0x19: // HookEntryInt
    {
        regs[2] = m_hookEntryInt.descriptorAddress;
        m_hookEntryInt.descriptorAddress = a0;

        std::ostringstream msg;
        msg << "HookEntryInt descriptor=0x" << std::hex << a0;
        m_logger.log(LogLevel::Debug, "bios", msg.str());
        return true;
    }
    case 0x20: // UnDeliverEvent(class, spec)
    {
        // PSX-SPX: UnDeliverEvent operates on class+spec, not handle.
        // We iterate all events matching the class and undedeliver them.
        // For simplicity we reuse the handle-based API by iterating all
        // possible handles.
        for (size_t i = 0; i < KernelEventTable::MAX_EVENTS; ++i)
        {
            u32 handle = KernelEventTable::HANDLE_BASE | static_cast<u32>(i << 4);
            const auto* ev = m_events.getEvent(handle);
            if (ev && ev->classId == a0 && ev->spec == static_cast<u16>(a1 & 0xFFFF))
            {
                m_events.undeliverEvent(handle);
            }
        }
        m_logger.log(LogLevel::Debug, "bios", "UnDeliverEvent");
        return true;
    }
    case 0x32:       // FileOpen - stub
        regs[2] = 0; // fail
        return true;
    case 0x3D: // putchar
    {
        char ch = static_cast<char>(a0 & 0xFF);
        std::ostringstream msg;
        msg << "BIOS B0 putchar: '" << ch << "'";
        m_logger.log(LogLevel::Info, "bios", msg.str());
        return true;
    }
    case 0x3F: // puts
    {
        const char* str = reinterpret_cast<const char*>(ramPointerConst(m_ram.data(), a0));
        std::ostringstream msg;
        msg << "BIOS B0 puts: \"";
        for (int i = 0; i < 256 && str[i] != 0; ++i)
        {
            msg << str[i];
        }
        msg << "\"";
        m_logger.log(LogLevel::Info, "bios", msg.str());
        return true;
    }
    case 0x46: // undelete(filename) - file API stub
    {
        regs[2] = 0;
        m_logger.log(LogLevel::Debug, "bios", "undelete (B0 0x46) - stub");
        return true;
    }
    case 0x47: // AddDevice
        regs[2] = 1;
        return true;
    case 0x4A: // InitCard
        return true;
    case 0x4B: // StartCard
        return true;
    case 0x56: // GetC0Table
        regs[2] = 0;
        return true;
    case 0x57: // GetB0Table
        regs[2] = 0;
        return true;
    case 0x5B: // ChangeClearPAD
        return true;
    default:
        return false;
    }
}

} // namespace runtime
} // namespace psxrecomp
