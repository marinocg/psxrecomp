/// MDEC DMA integration test -- simulates the movie-decode path through
/// the PsxSystem DMA engine: CDROM→RAM (DMA3), RAM→MDEC (DMA0),
/// MDEC→RAM (DMA1).
#include "psxrecomp/runtime/psx_system.h"

#include <sstream>
#include <stdexcept>

namespace
{
using psxrecomp::Address;
using psxrecomp::u32;
using psxrecomp::runtime::DmaController;
using psxrecomp::runtime::DmaPort;
using psxrecomp::runtime::PsxSystem;

constexpr u32 MDEC_CMD = psxrecomp::runtime::Mmio::MDEC_BASE;
constexpr u32 MDEC_CTRL = psxrecomp::runtime::Mmio::MDEC_BASE + 4;

constexpr u32 STATUS_FIFO_EMPTY = 1u << 31;
constexpr u32 STATUS_CMD_BUSY = 1u << 29;
constexpr u32 STATUS_DMA_OUT_REQ = 1u << 27;

constexpr u32 CTRL_RESET = 1u << 31;
constexpr u32 CTRL_DMA_IN = 1u << 30;
constexpr u32 CTRL_DMA_OUT = 1u << 29;

// DMA channel control values.
constexpr u32 DMA_FROM_RAM = 0x01000201u; // direction=fromRAM, syncMode=request, start
constexpr u32 DMA_TO_RAM = 0x01000200u;   // direction=toRAM, syncMode=request, start

Address dmaBase(DmaPort port)
{
    return DmaController::ChannelBase + DmaController::ChannelStride * static_cast<Address>(port);
}

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

[[noreturn]] void failHex(const char* label, u32 expected, u32 actual)
{
    std::ostringstream s;
    s << label << " expected=0x" << std::hex << expected << " actual=0x" << actual;
    throw std::runtime_error(s.str());
}

void requireEqual(const char* label, u32 expected, u32 actual)
{
    if (expected != actual)
    {
        failHex(label, expected, actual);
    }
}

// ---- DMA0 (MdecIn) feeds parameters, DMA1 (MdecOut) drains output ----

void testDma0ThenDma1MoviePath(PsxSystem& sys)
{
    // Reset MDEC.
    sys.writeMmioExplicit<u32>(MDEC_CTRL, CTRL_RESET);
    requireEqual("post-reset status", 0x80040000u, sys.readMmioExplicit<u32>(MDEC_CTRL));

    // Place fake compressed data in RAM at 0x10000.
    constexpr Address inputBase = 0x10000u;
    constexpr u32 paramWords = 0x20u; // 32 parameter words
    for (u32 i = 0; i < paramWords; ++i)
    {
        sys.write<u32>(inputBase + i * 4u, 0xFE00FE00u); // padding / no-op data
    }

    // Write decode command via MMIO: cmd=1, depth=1 (8-bit), paramWords.
    const u32 cmdWord = (1u << 29) | (1u << 27) | paramWords;
    sys.writeMmioExplicit<u32>(MDEC_CMD, cmdWord);

    // Verify busy is set.
    u32 status = sys.readMmioExplicit<u32>(MDEC_CTRL);
    require((status & STATUS_CMD_BUSY) != 0, "MDEC should be busy after command");

    // Enable DMA in + DMA out.
    sys.writeMmioExplicit<u32>(MDEC_CTRL, CTRL_DMA_IN | CTRL_DMA_OUT);

    // Trigger DMA0 (MdecIn): transfer paramWords from RAM to MDEC.
    const Address dma0 = dmaBase(DmaPort::MdecIn);
    sys.writeMmioExplicit<u32>(dma0 + 0x0, inputBase);
    sys.writeMmioExplicit<u32>(dma0 + 0x4, paramWords | (1u << 16));
    sys.writeMmioExplicit<u32>(dma0 + 0x8, DMA_FROM_RAM);

    // After DMA0 completes synchronously, command should be finished.
    status = sys.readMmioExplicit<u32>(MDEC_CTRL);
    require((status & STATUS_CMD_BUSY) == 0, "MDEC should not be busy after DMA0 input");
    require((status & STATUS_FIFO_EMPTY) == 0, "output should be available after decode");
    require((status & STATUS_DMA_OUT_REQ) != 0, "DMA out request should be asserted");

    // Trigger DMA1 (MdecOut): transfer output from MDEC to RAM.
    constexpr Address outputBase = 0x20000u;
    constexpr u32 drainWords = 0x20u;
    // Zero destination first.
    for (u32 i = 0; i < drainWords; ++i)
    {
        sys.write<u32>(outputBase + i * 4u, 0xDEADDEADu);
    }

    const Address dma1 = dmaBase(DmaPort::MdecOut);
    sys.writeMmioExplicit<u32>(dma1 + 0x0, outputBase);
    sys.writeMmioExplicit<u32>(dma1 + 0x4, drainWords | (1u << 16));
    sys.writeMmioExplicit<u32>(dma1 + 0x8, DMA_TO_RAM);

    // Verify at least the first word was written (placeholder zeros).
    const u32 firstWord = sys.read<u32>(outputBase);
    require(firstWord != 0xDEADDEADu, "DMA1 should have written output to RAM");
}

// ---- Verify busy does not stick high forever ----

void testBusyNotStuckAfterFullPipeline(PsxSystem& sys)
{
    sys.writeMmioExplicit<u32>(MDEC_CTRL, CTRL_RESET);

    constexpr Address inputBase = 0x30000u;
    constexpr u32 paramWords = 8u;
    for (u32 i = 0; i < paramWords; ++i)
    {
        sys.write<u32>(inputBase + i * 4u, 0u);
    }

    const u32 cmdWord = (1u << 29) | paramWords;
    sys.writeMmioExplicit<u32>(MDEC_CMD, cmdWord);
    sys.writeMmioExplicit<u32>(MDEC_CTRL, CTRL_DMA_IN | CTRL_DMA_OUT);

    const Address dma0 = dmaBase(DmaPort::MdecIn);
    sys.writeMmioExplicit<u32>(dma0 + 0x0, inputBase);
    sys.writeMmioExplicit<u32>(dma0 + 0x4, paramWords | (1u << 16));
    sys.writeMmioExplicit<u32>(dma0 + 0x8, DMA_FROM_RAM);

    const u32 status = sys.readMmioExplicit<u32>(MDEC_CTRL);
    require((status & STATUS_CMD_BUSY) == 0, "busy must clear after full pipeline (not stuck)");
}

// ---- Multiple decode cycles ----

void testMultipleDecodeCycles(PsxSystem& sys)
{
    constexpr Address inputBase = 0x40000u;
    constexpr Address outputBase = 0x50000u;
    constexpr u32 paramWords = 4u;

    for (int cycle = 0; cycle < 3; ++cycle)
    {
        sys.writeMmioExplicit<u32>(MDEC_CTRL, CTRL_RESET);

        for (u32 i = 0; i < paramWords; ++i)
        {
            sys.write<u32>(inputBase + i * 4u, static_cast<u32>(cycle));
        }

        const u32 cmdWord = (1u << 29) | (2u << 27) | paramWords;
        sys.writeMmioExplicit<u32>(MDEC_CMD, cmdWord);
        sys.writeMmioExplicit<u32>(MDEC_CTRL, CTRL_DMA_IN | CTRL_DMA_OUT);

        // DMA0 in.
        const Address dma0 = dmaBase(DmaPort::MdecIn);
        sys.writeMmioExplicit<u32>(dma0 + 0x0, inputBase);
        sys.writeMmioExplicit<u32>(dma0 + 0x4, paramWords | (1u << 16));
        sys.writeMmioExplicit<u32>(dma0 + 0x8, DMA_FROM_RAM);

        // DMA1 out.
        for (u32 i = 0; i < 0x20u; ++i)
        {
            sys.write<u32>(outputBase + i * 4u, 0xDEADDEADu);
        }
        const Address dma1 = dmaBase(DmaPort::MdecOut);
        sys.writeMmioExplicit<u32>(dma1 + 0x0, outputBase);
        sys.writeMmioExplicit<u32>(dma1 + 0x4, 0x20u | (1u << 16));
        sys.writeMmioExplicit<u32>(dma1 + 0x8, DMA_TO_RAM);

        require(sys.read<u32>(outputBase) != 0xDEADDEADu,
                "each decode cycle should produce DMA1 output");
    }
}

} // namespace

int main()
{
    PsxSystem sys;
    require(sys.initialize(), "system init failed");

    testDma0ThenDma1MoviePath(sys);
    testBusyNotStuckAfterFullPipeline(sys);
    testMultipleDecodeCycles(sys);

    return 0;
}
