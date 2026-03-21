#include "psxrecomp/runtime/psx_system.h"

#include "bios_helpers.h"

#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr Address SETJMP_RA_OFFSET = 0x00u;
constexpr Address SETJMP_SP_OFFSET = 0x04u;
constexpr Address SETJMP_FP_OFFSET = 0x08u;
constexpr Address SETJMP_S0_OFFSET = 0x0Cu;
constexpr Address SETJMP_GP_OFFSET = 0x2Cu;

std::string readBiosString(const std::vector<u8>& ram, u32 address, size_t maxLen = 1024)
{
    const char* ptr = reinterpret_cast<const char*>(ramPointerConst(ram.data(), address));
    std::string result;
    for (size_t i = 0; i < maxLen && ptr[i] != 0; ++i)
    {
        result.push_back(ptr[i]);
    }
    return result;
}

u32 readStackArg(const std::vector<u8>& ram, u32 address)
{
    u32 value = 0;
    std::memcpy(&value, ramPointerConst(ram.data(), address), sizeof(value));
    return value;
}
} // namespace

bool PsxSystem::callBiosVectorA0(u32 functionId, u32* regs)
{
    const u32 a0 = regs[4];
    const u32 a1 = regs[5];
    const u32 a2 = regs[6];

    switch (functionId)
    {
    case 0x13: // setjmp - store HookEntryInt longjmp state, return 0
    {
        write<u32>(a0 + SETJMP_RA_OFFSET, regs[Registers::RA]);
        write<u32>(a0 + SETJMP_SP_OFFSET, regs[Registers::SP]);
        write<u32>(a0 + SETJMP_FP_OFFSET, regs[Registers::FP]);
        for (u32 reg = Registers::S0; reg <= Registers::S7; ++reg)
        {
            const Address offset = SETJMP_S0_OFFSET + (reg - Registers::S0) * sizeof(u32);
            write<u32>(a0 + offset, regs[reg]);
        }
        write<u32>(a0 + SETJMP_GP_OFFSET, regs[Registers::GP]);
        regs[2] = 0; // $v0 = 0
        return true;
    }
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
    case 0x18: // strncmp
    {
        const char* s1Ptr = reinterpret_cast<const char*>(ramPointerConst(m_ram.data(), a0));
        const char* s2Ptr = reinterpret_cast<const char*>(ramPointerConst(m_ram.data(), a1));
        int result = 0;
        for (u32 i = 0; i < a2; ++i)
        {
            const int c1 = static_cast<unsigned char>(*s1Ptr);
            const int c2 = static_cast<unsigned char>(*s2Ptr);
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
    case 0x1B: // strlen
    {
        regs[2] = static_cast<u32>(
            std::strlen(reinterpret_cast<const char*>(ramPointerConst(m_ram.data(), a0))));
        return true;
    }
    case 0x27: // bcopy
    {
        const u8* src = ramPointerConst(m_ram.data(), a0);
        std::vector<u8> scratch(a2);
        std::memmove(scratch.data(), src, a2);
        std::ostringstream detail;
        detail << "src=0x" << std::hex << a0;
        copyBufferToRam(a1, scratch.data(), a2, a2, m_debugOverlay.lastProgramCounter(), "A0:bcopy",
                        detail.str());
        return true;
    }
    case 0x28: // bzero / memset 0
    {
        fillBufferToRam(a0, 0, a1, m_debugOverlay.lastProgramCounter(), "A0:bzero", "");
        return true;
    }
    case 0x2A: // memcpy
    {
        const u8* src = ramPointerConst(m_ram.data(), a1);
        std::vector<u8> scratch(a2);
        std::memcpy(scratch.data(), src, a2);
        std::ostringstream detail;
        detail << "src=0x" << std::hex << a1;
        copyBufferToRam(a0, scratch.data(), a2, a2, m_debugOverlay.lastProgramCounter(),
                        "A0:memcpy", detail.str());
        regs[2] = a0;
        return true;
    }
    case 0x2B: // memset
    {
        std::ostringstream detail;
        detail << "value=0x" << std::hex << (a1 & 0xFF);
        fillBufferToRam(a0, static_cast<u8>(a1 & 0xFF), a2, m_debugOverlay.lastProgramCounter(),
                        "A0:memset", detail.str());
        regs[2] = a0;
        return true;
    }
    case 0x33: // malloc
    {
        u32 size = (a0 + 3) & ~3u; // 4-byte align
        if (m_biosHeapBase != 0 && size > 0 &&
            m_biosHeapCursor + size <= m_biosHeapBase + m_biosHeapSize)
        {
            regs[2] = m_biosHeapCursor;
            m_biosHeapCursor += size;
        }
        else if (size > 0)
        {
            // Fallback bump allocator for games that skip InitHeap.
            static u32 fallbackTop = 0x801F0000u;
            if (fallbackTop >= (0x80010000u + size))
            {
                fallbackTop -= size;
                regs[2] = fallbackTop;
            }
            else
            {
                regs[2] = 0;
            }
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
        m_biosHeapBase = a0;
        m_biosHeapSize = a1;
        m_biosHeapCursor = a0;
        {
            std::ostringstream msg;
            msg << "InitHeap base=0x" << std::hex << a0 << " size=0x" << a1;
            m_logger.log(LogLevel::Debug, "bios", msg.str());
        }
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
    case 0x3F: // printf
    {
        const std::string format = readBiosString(m_ram, a0);
        std::ostringstream rendered;
        u32 stackArgAddress = regs[Registers::SP] + 16;
        u8 nextRegArg = Registers::A1;
        const auto nextArg = [&]() -> u32
        {
            if (nextRegArg <= Registers::A3)
            {
                return regs[nextRegArg++];
            }
            const u32 value = readStackArg(m_ram, stackArgAddress);
            stackArgAddress += 4;
            return value;
        };

        for (size_t i = 0; i < format.size(); ++i)
        {
            const char ch = format[i];
            if (ch != '%')
            {
                rendered << ch;
                continue;
            }
            if (i + 1 < format.size() && format[i + 1] == '%')
            {
                rendered << '%';
                ++i;
                continue;
            }

            size_t specIndex = i + 1;
            while (specIndex < format.size() &&
                   (format[specIndex] == '-' || format[specIndex] == '+' ||
                    format[specIndex] == ' ' || format[specIndex] == '#' ||
                    format[specIndex] == '0'))
            {
                ++specIndex;
            }
            while (specIndex < format.size() &&
                   std::isdigit(static_cast<unsigned char>(format[specIndex])) != 0)
            {
                ++specIndex;
            }
            if (specIndex < format.size() && format[specIndex] == '.')
            {
                ++specIndex;
                while (specIndex < format.size() &&
                       std::isdigit(static_cast<unsigned char>(format[specIndex])) != 0)
                {
                    ++specIndex;
                }
            }
            while (
                specIndex < format.size() &&
                (format[specIndex] == 'h' || format[specIndex] == 'l' || format[specIndex] == 'L'))
            {
                ++specIndex;
            }
            if (specIndex >= format.size())
            {
                break;
            }

            const char spec = format[specIndex];
            const u32 rawArg = nextArg();
            switch (spec)
            {
            case 'c':
                rendered << static_cast<char>(rawArg & 0xFFu);
                break;
            case 's':
                rendered << readBiosString(m_ram, rawArg);
                break;
            case 'd':
            case 'i':
                rendered << static_cast<s32>(rawArg);
                break;
            case 'u':
                rendered << rawArg;
                break;
            case 'x':
            case 'X':
                rendered << std::hex;
                if (spec == 'X')
                {
                    rendered.setf(std::ios::uppercase);
                }
                rendered << rawArg;
                rendered << std::dec;
                rendered.unsetf(std::ios::uppercase);
                break;
            case 'p':
                rendered << "0x" << std::hex << rawArg << std::dec;
                break;
            default:
                rendered << '%' << spec;
                break;
            }
            i = specIndex;
        }

        regs[2] = static_cast<u32>(rendered.str().size());
        m_logger.log(LogLevel::Info, "bios", "BIOS printf: \"" + rendered.str() + "\"");
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
        // PSX-SPX sequence:
        //   GP1(04h)=2 (DMA CPU->GP0), ack GPU flag in DICR, DPCR|=0x800,
        //   CHCR setup/start.
        // Some BIOS revisions destructively zero DICR here, which clobbers
        // channel enables that other subsystems (CDROM, SPU) have already
        // configured.  We only acknowledge the GPU channel flag (bit 26)
        // to match corrected BIOS behaviour and avoid breaking DMA IRQs.
        constexpr Address gpuDmaBase =
            DmaController::ChannelBase +
            static_cast<Address>(DmaPort::Gpu) * DmaController::ChannelStride;
        writeMmio32(Mmio::GPU_GP1, 0x04000002u);
        {
            // Preserve control bits (0-23), acknowledge GPU channel flag only.
            // Must NOT feed back existing flags (24-30) because those bits use
            // write-1-to-clear semantics and would accidentally ack other channels.
            const u32 curDicr = readMmio32(DmaController::InterruptReg);
            constexpr u32 gpuChannelFlag = 1u << 26;
            writeMmio32(DmaController::InterruptReg, (curDicr & 0x00FFFFFFu) | gpuChannelFlag);
        }
        const u32 dpcr = readMmio32(DmaController::ControlReg);
        writeMmio32(DmaController::ControlReg, dpcr | 0x00000800u);
        writeMmio32(gpuDmaBase + 0x0, a0 & 0x1FFFFCu);
        writeMmio32(gpuDmaBase + 0x4, 0u);
        writeMmio32(gpuDmaBase + 0x8, 0x01000401u);
        return true;
    }
    case 0x4E: // gpu_sync
    {
        constexpr u32 gpuStatDmaRequest = 1u << 28;
        constexpr Address gpuDmaChcr =
            DmaController::ChannelBase +
            static_cast<Address>(DmaPort::Gpu) * DmaController::ChannelStride + 0x8;

        // If GPU DMA mode is enabled, ensure channel transfer has quiesced
        // and then force GP1(04h)=0 (DMA off), mirroring BIOS behavior.
        bool timeout = false;
        const u32 dmaDirection = (m_gpu.pollStatus() >> 29) & 0x3u;
        if (dmaDirection != 0)
        {
            const u32 chcr = readMmio32(gpuDmaChcr);
            if ((chcr & 0x01000000u) != 0)
            {
                handleDmaTransfer(DmaPort::Gpu);
            }

            if ((m_gpu.pollStatus() & gpuStatDmaRequest) == 0)
            {
                timeout = true;
            }
            else
            {
                writeMmio32(Mmio::GPU_GP1, 0x04000000u);
            }
        }

        if (dmaDirection == 0 && (m_gpu.pollStatus() & gpuStatDmaRequest) == 0)
        {
            timeout = true;
        }

        regs[2] = timeout ? 0xFFFFFFFFu : 0u;
        return true;
    }
    case 0x70: // GPU_init
    {
        m_gpu.writeStatus(0x00000000u);
        m_gpu.writeStatus(0x08000001u);
        m_logger.log(LogLevel::Debug, "bios", "GPU_init (A0 0x70)");
        return true;
    }
    case 0x71: // _96_init - initialize BIOS-facing CD-ROM interrupt state
    {
        initializeBiosCdromState(a0);
        regs[2] = 0;
        m_logger.log(LogLevel::Debug, "bios", "_96_init (A0 0x71)");
        return true;
    }
    case 0x72: // _96_remove - bug-compatible no-op in retail BIOS
    {
        regs[2] = 0;
        m_logger.log(LogLevel::Debug, "bios", "_96_remove (A0 0x72)");
        return true;
    }
    // CD-ROM BIOS functions — delegated to psx_system_bios_cd.cpp
    case 0x54: // CdInit
    case 0x56: // CdRemove
    case 0x78: // CdAsyncSeekL
    case 0x7C: // CdAsyncGetStatus
    case 0x7E: // CdAsyncReadSector
    case 0x81: // CdAsyncSetMode
    case 0x95: // CdInitSubFunc
        return callBiosCdFunction(functionId, regs);
    default:
        return false;
    }
}

} // namespace runtime
} // namespace psxrecomp
