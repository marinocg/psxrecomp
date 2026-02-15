#include "psxrecomp/runtime/psx_system.h"

#include <cstring>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

void PsxSystem::callBiosVector(u32 vector, u32* regs, size_t regCount)
{
    if (regs == nullptr || regCount < 32)
    {
        m_logger.log(LogLevel::Warn, "bios", "BIOS vector call with insufficient register file");
        return;
    }

    // Function number is in $t1 (register 9).
    const u32 functionId = regs[9];
    // Arguments are in $a0-$a3 (registers 4-7).
    const u32 a0 = regs[4];
    const u32 a1 = regs[5];
    const u32 a2 = regs[6];
    (void)a2;

    const char* vectorName = "??";
    if (vector == 0xA0)
    {
        vectorName = "A0";
    }
    else if (vector == 0xB0)
    {
        vectorName = "B0";
    }
    else if (vector == 0xC0)
    {
        vectorName = "C0";
    }

    if (const char* traceBios = std::getenv("PSXRECOMP_TRACE_BIOS"))
    {
        if (traceBios[0] == '1')
        {
            std::ostringstream trace;
            trace << "BIOS " << vectorName << "(0x" << std::hex << functionId << ")" << " a0=0x"
                  << a0 << " a1=0x" << a1 << " a2=0x" << a2;
            m_logger.log(LogLevel::Info, "bios_trace", trace.str());
        }
    }

    // Implement minimal BIOS functions needed by psn00bsdk demos.
    if (vector == 0xA0)
    {
        switch (functionId)
        {
        case 0x13:       // setjmp - store context, return 0
            regs[2] = 0; // $v0 = 0
            return;
        case 0x17: // strcmp
        {
            const char* s1Ptr = reinterpret_cast<const char*>(m_ram.data() + (a0 & 0x1FFFFF));
            const char* s2Ptr = reinterpret_cast<const char*>(m_ram.data() + (a1 & 0x1FFFFF));
            int result = 0;
            while (true)
            {
                int c1 = static_cast<unsigned char>(*s1Ptr);
                int c2 = static_cast<unsigned char>(*s2Ptr);
                if (c1 != c2 || c1 == 0)
                {
                    result = c1 - c2;
                    break;
                }
                ++s1Ptr;
                ++s2Ptr;
            }
            regs[2] = static_cast<u32>(result);
            return;
        }
        case 0x19: // strcpy
        {
            u8* dst = m_ram.data() + (a0 & 0x1FFFFF);
            const u8* src = m_ram.data() + (a1 & 0x1FFFFF);
            while (*src != 0)
            {
                *dst++ = *src++;
            }
            *dst = 0;
            regs[2] = a0;
            return;
        }
        case 0x28: // bzero / memset 0
        {
            u8* dst = m_ram.data() + (a0 & 0x1FFFFF);
            std::memset(dst, 0, a1);
            return;
        }
        case 0x2A: // memcpy
        {
            u8* dst = m_ram.data() + (a0 & 0x1FFFFF);
            const u8* src = m_ram.data() + (a1 & 0x1FFFFF);
            std::memcpy(dst, src, a2);
            regs[2] = a0;
            return;
        }
        case 0x2B: // memset
        {
            u8* dst = m_ram.data() + (a0 & 0x1FFFFF);
            std::memset(dst, static_cast<int>(a1 & 0xFF), a2);
            regs[2] = a0;
            return;
        }
        case 0x33: // malloc - simple bump allocator stub
        {
            // Very simple heap: allocate from top of RAM downwards.
            // Use a static variable to track allocation offset.
            static u32 heapTop = 0x801F0000u;
            u32 size = (a0 + 7) & ~7u; // 8-byte align
            if (size > 0 && heapTop >= (0x80010000u + size))
            {
                heapTop -= size;
                regs[2] = heapTop;
            }
            else
            {
                regs[2] = 0; // allocation failed
            }
            return;
        }
        case 0x34: // free - stub (no-op)
            return;
        case 0x39: // InitHeap
        {
            // InitHeap(void* base, int size) - just acknowledge it
            m_logger.log(LogLevel::Debug, "bios", "InitHeap acknowledged");
            return;
        }
        case 0x3C: // putchar
        {
            char ch = static_cast<char>(a0 & 0xFF);
            std::ostringstream msg;
            msg << "BIOS putchar: '" << ch << "' (0x" << std::hex << (a0 & 0xFF) << ")";
            m_logger.log(LogLevel::Info, "bios", msg.str());
            return;
        }
        case 0x3E: // puts
        {
            const char* str = reinterpret_cast<const char*>(m_ram.data() + (a0 & 0x1FFFFF));
            std::ostringstream msg;
            msg << "BIOS puts: \"";
            for (int i = 0; i < 256 && str[i] != 0; ++i)
            {
                msg << str[i];
            }
            msg << "\"";
            m_logger.log(LogLevel::Info, "bios", msg.str());
            return;
        }
        case 0x44: // FlushCache
            return;
        case 0x49: // GPU_cw (send GP0 command)
            m_gpu.writeCommand(a0);
            return;
        case 0x4A: // GPU_cwp (send GP0 command list)
        {
            u32 addr = a0 & 0x1FFFFF;
            for (u32 i = 0; i < a1 && addr + 3 < MemoryMap::RAM_SIZE; ++i)
            {
                u32 word = 0;
                std::memcpy(&word, m_ram.data() + addr, sizeof(u32));
                m_gpu.writeCommand(word);
                addr += 4;
            }
            return;
        }
        case 0x70: // GPU_init - reset GPU to default state
        {
            // Send GP1(00h) = Reset GPU.
            m_gpu.writeStatus(0x00000000u);
            // Send GP1(08h) = Display Mode (320x240, NTSC).
            m_gpu.writeStatus(0x08000001u);
            m_logger.log(LogLevel::Debug, "bios", "GPU_init (A0 0x70)");
            return;
        }
        default:
            break;
        }
    }
    else if (vector == 0xB0)
    {
        switch (functionId)
        {
        case 0x00: // alloc_kernel_memory
        {
            static u32 kernelHeap = 0x80010000u;
            u32 size = (a0 + 7) & ~7u;
            regs[2] = kernelHeap;
            kernelHeap += size;
            return;
        }
        case 0x07: // DeliverEvent
            return;
        case 0x08:          // OpenEvent
            regs[2] = 0x10; // return fake event handle
            return;
        case 0x09: // CloseEvent
            return;
        case 0x0A: // WaitEvent - stub: return immediately
            return;
        case 0x0B:       // TestEvent
            regs[2] = 1; // event already occurred
            return;
        case 0x0C: // EnableEvent
            return;
        case 0x0D: // DisableEvent
            return;
        case 0x12: // InitPad
            return;
        case 0x13: // StartPad
            return;
        case 0x17: // ReturnFromException
            return;
        case 0x18: // SetDefaultExitFromException
            return;
        case 0x19: // SetCustomExitFromException
            return;
        case 0x20: // UnDeliverEvent
            return;
        case 0x32:       // FileOpen - stub
            regs[2] = 0; // fail
            return;
        case 0x3D: // putchar
        {
            char ch = static_cast<char>(a0 & 0xFF);
            std::ostringstream msg;
            msg << "BIOS B0 putchar: '" << ch << "'";
            m_logger.log(LogLevel::Info, "bios", msg.str());
            return;
        }
        case 0x3F: // puts
        {
            const char* str = reinterpret_cast<const char*>(m_ram.data() + (a0 & 0x1FFFFF));
            std::ostringstream msg;
            msg << "BIOS B0 puts: \"";
            for (int i = 0; i < 256 && str[i] != 0; ++i)
            {
                msg << str[i];
            }
            msg << "\"";
            m_logger.log(LogLevel::Info, "bios", msg.str());
            return;
        }
        case 0x46: // GPU_sync - wait for GPU to finish drawing
        {
            // In recompiled code the GPU is software-rendered and always
            // finishes immediately, so return 0 (idle).
            regs[2] = 0; // $v0 = 0 (GPU idle)
            m_logger.log(LogLevel::Debug, "bios", "GPU_sync (B0 0x46)");
            return;
        }
        case 0x47: // AddDevice
            regs[2] = 1;
            return;
        case 0x4A: // InitCard
            return;
        case 0x4B: // StartCard
            return;
        case 0x56: // GetC0Table
            regs[2] = 0;
            return;
        case 0x57: // GetB0Table
            regs[2] = 0;
            return;
        case 0x5B: // ChangeClearPAD
            return;
        default:
            break;
        }
    }
    else if (vector == 0xC0)
    {
        switch (functionId)
        {
        case 0x00: // EnqueueTimerAndVblankIrqs
            return;
        case 0x01: // EnqueueSyscallHandler
            return;
        case 0x02: // SysEnqIntRP
            return;
        case 0x03: // SysDeqIntRP
            return;
        case 0x07: // InstallExceptionHandlers
            return;
        case 0x08: // SysInitMemory
            return;
        case 0x09: // SysInitKernelVariables
            return;
        case 0x0A: // ChangeClearRCnt
            return;
        case 0x0C: // InitDefInt
            return;
        case 0x12: // InstallDevices
            return;
        case 0x1C: // AdjustA0Table
            return;
        default:
            break;
        }
    }

    // Unhandled BIOS call - log it.
    std::ostringstream stream;
    stream << "Unhandled BIOS vector " << vectorName << "(0x" << std::hex << functionId << ")"
           << " a0=0x" << a0 << " a1=0x" << a1;
    m_logger.log(LogLevel::Warn, "bios", stream.str());
}

} // namespace runtime
} // namespace psxrecomp
