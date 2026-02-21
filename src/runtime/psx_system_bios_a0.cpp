#include "psxrecomp/runtime/psx_system.h"

#include "bios_helpers.h"

#include <cstring>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

bool PsxSystem::callBiosVectorA0(u32 functionId, u32* regs)
{
    const u32 a0 = regs[4];
    const u32 a1 = regs[5];
    const u32 a2 = regs[6];

    switch (functionId)
    {
    case 0x13:       // setjmp - store context, return 0
        regs[2] = 0; // $v0 = 0
        return true;
    case 0x17: // strcmp
    {
        const char* s1Ptr = reinterpret_cast<const char*>(ramPointerConst(m_ram.data(), a0));
        const char* s2Ptr = reinterpret_cast<const char*>(ramPointerConst(m_ram.data(), a1));
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
        return true;
    }
    case 0x19: // strcpy
    {
        u8* dst = ramPointer(m_ram.data(), a0);
        const u8* src = ramPointerConst(m_ram.data(), a1);
        while (*src != 0)
        {
            *dst++ = *src++;
        }
        *dst = 0;
        regs[2] = a0;
        return true;
    }
    case 0x28: // bzero / memset 0
    {
        u8* dst = ramPointer(m_ram.data(), a0);
        std::memset(dst, 0, a1);
        return true;
    }
    case 0x2A: // memcpy
    {
        u8* dst = ramPointer(m_ram.data(), a0);
        const u8* src = ramPointerConst(m_ram.data(), a1);
        std::memcpy(dst, src, a2);
        regs[2] = a0;
        return true;
    }
    case 0x2B: // memset
    {
        u8* dst = ramPointer(m_ram.data(), a0);
        std::memset(dst, static_cast<int>(a1 & 0xFF), a2);
        regs[2] = a0;
        return true;
    }
    case 0x33: // malloc - simple bump allocator stub
    {
        static u32 heapTop = 0x801F0000u;
        u32 size = (a0 + 7) & ~7u; // 8-byte align
        if (size > 0 && heapTop >= (0x80010000u + size))
        {
            heapTop -= size;
            regs[2] = heapTop;
        }
        else
        {
            regs[2] = 0;
        }
        return true;
    }
    case 0x34: // free - stub (no-op)
        return true;
    case 0x39: // InitHeap
    {
        m_logger.log(LogLevel::Debug, "bios", "InitHeap acknowledged");
        return true;
    }
    case 0x3C: // putchar
    {
        char ch = static_cast<char>(a0 & 0xFF);
        std::ostringstream msg;
        msg << "BIOS putchar: '" << ch << "' (0x" << std::hex << (a0 & 0xFF) << ")";
        m_logger.log(LogLevel::Info, "bios", msg.str());
        return true;
    }
    case 0x3E: // puts
    {
        const char* str = reinterpret_cast<const char*>(ramPointerConst(m_ram.data(), a0));
        std::ostringstream msg;
        msg << "BIOS puts: \"";
        for (int i = 0; i < 256 && str[i] != 0; ++i)
        {
            msg << str[i];
        }
        msg << "\"";
        m_logger.log(LogLevel::Info, "bios", msg.str());
        return true;
    }
    case 0x44: // FlushCache
        return true;
    case 0x49: // GPU_cw (send GP0 command)
        m_gpu.writeCommand(a0);
        return true;
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
        return true;
    }
    case 0x4B: // send_gpu_linked_list (GPU ordering table DMA)
    {
        u32 nodeAddr = a0 & 0x1FFFFC;
        for (u32 safety = 0; safety < 0x100000u; ++safety)
        {
            u32 header = 0;
            std::memcpy(&header, m_ram.data() + nodeAddr, sizeof(u32));
            const u32 commandCount = (header >> 24) & 0xFF;
            for (u32 i = 0; i < commandCount; ++i)
            {
                const u32 cmdAddr = (nodeAddr + (i + 1) * sizeof(u32)) & 0x1FFFFC;
                u32 word = 0;
                std::memcpy(&word, m_ram.data() + cmdAddr, sizeof(u32));
                m_gpu.writeCommand(word);
            }
            const u32 next = header & 0x00FFFFFF;
            if (next == 0x00FFFFFF)
            {
                break;
            }
            nodeAddr = next & 0x1FFFFC;
        }
        return true;
    }
    case 0x70: // GPU_init
    {
        m_gpu.writeStatus(0x00000000u);
        m_gpu.writeStatus(0x08000001u);
        m_logger.log(LogLevel::Debug, "bios", "GPU_init (A0 0x70)");
        return true;
    }
    case 0x72: // _96_init - CD-ROM initialization (stub)
    {
        m_logger.log(LogLevel::Debug, "bios", "_96_init (A0 0x72) - stub");
        return true;
    }
    default:
        return false;
    }
}

} // namespace runtime
} // namespace psxrecomp
