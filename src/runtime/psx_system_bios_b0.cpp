#include "psxrecomp/runtime/psx_system.h"

#include "bios_helpers.h"
#include "irq_trace_utils.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

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
        const auto* ev = m_events.getEvent(a0);
        if (ev && ev->mode == EventMode::NoCallback)
        {
            // PSX-SPX: For NoCallback events, WaitEvent blocks the
            // calling thread until the event is delivered by an IRQ.
            // We emulate this by busy-waiting while pumping hardware.
            bool delivered = waitForEvent(a0);
            regs[2] = delivered ? 1u : 0u;
        }
        else
        {
            // Callback-mode events: the delivery is handled via the
            // interrupt dispatcher invoking the callback directly.
            // WaitEvent returns immediately for these.
            m_logger.log(LogLevel::Debug, "bios",
                         "WaitEvent (callback-mode or invalid handle — non-blocking)");
            regs[2] = 1;
        }
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
    case 0x15: // PAD_init2(buf1, siz1, buf2, siz2)
        // Accept controller init and report a dual-port pad setup present.
        regs[2] = 2;
        return true;
    case 0x17: // ReturnFromException
        // In callback/IRQ context this exits the current callback and returns
        // to the interrupted execution point. IRQ epilogue COP0 restoration
        // is owned by PsxSystem::serviceInterrupts().
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
    case 0x32: // FileOpen(filename, accessmode)
    {
        const char* filename = reinterpret_cast<const char*>(ramPointerConst(m_ram.data(), a0));
        int fd = m_biosFt.fileOpen(filename ? filename : "");
        regs[2] = static_cast<u32>(fd); // -1 (0xFFFFFFFF) on failure
        {
            std::ostringstream msg;
            msg << "FileOpen \"" << (filename ? filename : "(null)") << "\" => fd=" << fd;
            m_logger.log(LogLevel::Debug, "bios", msg.str());
        }
        return true;
    }
    case 0x33: // FileSeek(fd, offset, seektype)
    {
        int fd = static_cast<int>(a0);
        int offset = static_cast<int>(a1);
        int whence = static_cast<int>(a2);
        int result = m_biosFt.fileSeek(fd, offset, whence);
        regs[2] = static_cast<u32>(result);
        return true;
    }
    case 0x34: // FileRead(fd, dst, length)
    {
        int fd = static_cast<int>(a0);
        u8* dst = ramPointer(m_ram.data(), a1);
        u32 length = a2;
        int result = m_biosFt.fileRead(fd, dst, length);
        regs[2] = static_cast<u32>(result);
        return true;
    }
    case 0x36: // FileClose(fd)
    {
        int fd = static_cast<int>(a0);
        bool ok = m_biosFt.fileClose(fd);
        regs[2] = ok ? static_cast<u32>(fd) : 0xFFFFFFFFu;
        return true;
    }
    case 0x42: // firstfile(filename, direntry)
    {
        const char* pattern = reinterpret_cast<const char*>(ramPointerConst(m_ram.data(), a0));
        const auto* entry = m_biosFt.firstFile(pattern ? pattern : "");
        if (entry && a1 != 0)
        {
            // Write the DirEntry struct at a1 in PSX RAM.
            // PSX DirEntry layout: name[20] @ +0, attr u32 @ +20, size u32 @ +24, (next ptr etc.)
            u8* dirEntryPtr = ramPointer(m_ram.data(), a1);
            std::memset(dirEntryPtr, 0, 40);
            size_t nameLen = std::min(entry->name.size(), static_cast<size_t>(19));
            std::memcpy(dirEntryPtr, entry->name.c_str(), nameLen);
            dirEntryPtr[nameLen] = 0;
            u32 attr = (entry->flags & 0x02) ? 0x10u : 0x00u; // directory flag
            std::memcpy(dirEntryPtr + 20, &attr, 4);
            u32 sz = entry->size;
            std::memcpy(dirEntryPtr + 24, &sz, 4);
        }
        regs[2] = entry ? a1 : 0u;
        return true;
    }
    case 0x43: // nextfile(direntry)
    {
        const auto* entry = m_biosFt.nextFile();
        if (entry && a0 != 0)
        {
            u8* dirEntryPtr = ramPointer(m_ram.data(), a0);
            std::memset(dirEntryPtr, 0, 40);
            size_t nameLen = std::min(entry->name.size(), static_cast<size_t>(19));
            std::memcpy(dirEntryPtr, entry->name.c_str(), nameLen);
            dirEntryPtr[nameLen] = 0;
            u32 attr = (entry->flags & 0x02) ? 0x10u : 0x00u;
            std::memcpy(dirEntryPtr + 20, &attr, 4);
            u32 sz = entry->size;
            std::memcpy(dirEntryPtr + 24, &sz, 4);
        }
        regs[2] = entry ? a0 : 0u;
        return true;
    }
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
        regs[2] = BIOS_C0_TABLE_ADDRESS;
        return true;
    case 0x57: // GetB0Table
        regs[2] = BIOS_B0_TABLE_ADDRESS;
        return true;
    case 0x5B: // ChangeClearPAD
        return true;
    default:
        return false;
    }
}

} // namespace runtime
} // namespace psxrecomp
