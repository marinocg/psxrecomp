#include "psxrecomp/runtime/psx_system.h"
#include "runtime_test_sections.h"

#include <cassert>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace MemoryMap = psxrecomp::MemoryMap;

namespace
{
void ackCdromIrq(psxrecomp::runtime::PsxSystem& system)
{
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 1u);
    // Drain all response bytes so none remain in the ack buffer after the ACK.
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0u);
    while ((system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0) &
            (1u << 5)) != 0u)
    {
        (void)system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1);
    }
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 1u);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3, 0x07u);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0u);
    system.tickCpuCycles(1);
}
} // namespace

int main()
{
    using psxrecomp::Address;
    using psxrecomp::runtime::DmaController;
    using psxrecomp::runtime::DmaPort;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::PsxSystem;

    assert(::setenv("PSXRECOMP_WATCH_WRITE", "0x80056598,0x800577bc-0x800577bf", 1) == 0);
    {
        PsxSystem watchedSystem;
        assert(watchedSystem.initialize());
        watchedSystem.debugOverlay().setLastProgramCounter(0x80012345u);
        watchedSystem.write<psxrecomp::u32>(0x80056598u, 0x11223344u);
        watchedSystem.debugOverlay().setLastProgramCounter(0x80012349u);
        watchedSystem.write<psxrecomp::u16>(0x800577BCu, 0x5566u);
        watchedSystem.write<psxrecomp::u32>(0x80058000u, 0xDEADBEEFu);

        const std::string watchedSummary = watchedSystem.stallClassifier().classify();
        assert(watchedSummary.find("Last watched RAM writes") != std::string::npos);
        assert(watchedSummary.find("pc=0x80012345") != std::string::npos);
        assert(watchedSummary.find("pc=0x80012349") != std::string::npos);
        assert(watchedSummary.find("addr=0x80056598") != std::string::npos);
        assert(watchedSummary.find("addr=0x800577bc") != std::string::npos);
        assert(watchedSummary.find("addr=0x80058000") == std::string::npos);
    }
    ::unsetenv("PSXRECOMP_WATCH_WRITE");

    if (::setenv("PSXRECOMP_HEAP_VALIDATE", "1", 1) != 0)
    {
        throw std::runtime_error("failed to enable PSXRECOMP_HEAP_VALIDATE");
    }
    {
        PsxSystem heapValidateSystem;
        if (!heapValidateSystem.initialize())
        {
            throw std::runtime_error("failed to initialize heap validation runtime test system");
        }

        // Configure a test validator (SentinelBlockChain, profile-driven).
        psxrecomp::runtime::ValidatorConfig testValidator;
        testValidator.name = "test_allocator";
        testValidator.type = psxrecomp::runtime::ValidatorType::SentinelBlockChain;
        testValidator.enabledWhen.type = "nonzero_u32";
        testValidator.enabledWhen.address = 0x800565B0u;
        testValidator.currentRoot = 0x800577BCu;
        testValidator.backupRoot = 0x80057F38u;
        testValidator.header.sizeMask = 0xFFFFFFFCu;
        testValidator.header.freeBit = 0x1u;
        testValidator.header.sentinel = 0xFFFFFFFEu;
        testValidator.maxNodes = 64;
        heapValidateSystem.diagValidators().configure({testValidator});

        // Allocator not initialized (enable flag at 0x800565B0 is zero).
        // Validator should skip and report passed with "skipped".
        bool sawSkipLog = false;
        heapValidateSystem.logger().setMinLevel(psxrecomp::runtime::LogLevel::Info);
        heapValidateSystem.logger().setCallback(
            [&sawSkipLog](const psxrecomp::runtime::LogEvent& event)
            {
                if (event.category == "heap" && event.message.find("skipped") != std::string::npos)
                {
                    sawSkipLog = true;
                }
            });
        heapValidateSystem.validateAllocatorHeapBoundary("CD IRQ callback", 0x3u);
        if (!sawSkipLog)
        {
            throw std::runtime_error("expected heap validation skip when enable condition not met");
        }

        // Initialize allocator: enable flag + scan/backup pointers + valid chain.
        heapValidateSystem.write<psxrecomp::u32>(0x800565B0u, 1u);
        heapValidateSystem.write<psxrecomp::u32>(0x800577BCu, 0x80060000u);
        heapValidateSystem.write<psxrecomp::u32>(0x80057F38u, 0x80060000u);
        heapValidateSystem.write<psxrecomp::u32>(0x80060000u, 0x20u);
        heapValidateSystem.write<psxrecomp::u32>(0x80060024u, 0xFFFFFFFEu);
        heapValidateSystem.validateAllocatorHeapCallBoundary(0x80011A58u);

        // Corrupt the heap chain (zero-size header should fail validation).
        heapValidateSystem.write<psxrecomp::u32>(0x80060000u, 0u);
        bool heapValidationThrew = false;
        try
        {
            heapValidateSystem.validateAllocatorHeapCallBoundary(0x80011A58u);
        }
        catch (const std::runtime_error& error)
        {
            heapValidationThrew =
                std::string(error.what()).find("allocator call return") != std::string::npos;
        }
        if (!heapValidationThrew)
        {
            throw std::runtime_error("expected allocator call heap validation failure");
        }
    }
    ::unsetenv("PSXRECOMP_HEAP_VALIDATE");

    // GPU wait tracing is now profile-driven via DiagTracepointEngine.
    {
        PsxSystem tracepointSystem;
        if (!tracepointSystem.initialize())
        {
            throw std::runtime_error("failed to initialize tracepoint test system");
        }

        psxrecomp::runtime::TracepointConfig gpuTracepoint;
        gpuTracepoint.name = "test_gpu_wait";
        gpuTracepoint.pcRangeStart = 0x8003E400u;
        gpuTracepoint.pcRangeEnd = 0x8003E600u;
        gpuTracepoint.mmioReads = {psxrecomp::runtime::Mmio::GPU_GP1};
        tracepointSystem.diagTracepoints().configure({gpuTracepoint});

        std::vector<std::string> traceLogs;
        tracepointSystem.logger().setMinLevel(psxrecomp::runtime::LogLevel::Info);
        tracepointSystem.logger().setCallback(
            [&traceLogs](const psxrecomp::runtime::LogEvent& event)
            {
                if (event.category == "tracepoint")
                {
                    traceLogs.push_back(event.message);
                }
            });

        // PC outside range → no entry trace.
        tracepointSystem.observeProgramCounter(0x80040000u);

        // PC inside range → entry trace.
        tracepointSystem.observeProgramCounter(0x8003E4F0u);

        // MMIO read inside range → mmio_read trace.
        tracepointSystem.readMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1);

        // PC outside range → exit trace.
        tracepointSystem.observeProgramCounter(0x80050000u);

        auto hasTraceLog = [&traceLogs](const std::string& needle)
        {
            for (const std::string& message : traceLogs)
            {
                if (message.find(needle) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        };

        if (!hasTraceLog("event=entry pc=0x8003e4f0"))
        {
            throw std::runtime_error("expected tracepoint entry log");
        }
        if (!hasTraceLog("event=mmio_read"))
        {
            throw std::runtime_error("expected tracepoint mmio_read log");
        }
        if (!hasTraceLog("event=exit"))
        {
            throw std::runtime_error("expected tracepoint exit log");
        }
    }

    // Display timing tracing is now profile-driven via DiagTracepointEngine.
    // Verify that Timer1 display-line clock still advances normally.
    {
        PsxSystem displaySystem;
        if (!displaySystem.initialize())
        {
            throw std::runtime_error("failed to initialize display timing test system");
        }

        displaySystem.writeMmioExplicit<psxrecomp::u16>(
            psxrecomp::runtime::Mmio::TIMER_BASE + 0x14u, 0x0100u);
        displaySystem.tickCpuCycles(564480u);
        if (displaySystem.timers().readCounter(1) == 0)
        {
            throw std::runtime_error("expected Timer1 display-line clock to advance");
        }
    }

    PsxSystem system;
    assert(system.initialize());

    Address ramEnd = MemoryMap::RAM_BASE + MemoryMap::RAM_SIZE - sizeof(psxrecomp::u32);
    system.write<psxrecomp::u32>(ramEnd, 0xDEADBEEF);
    assert(system.read<psxrecomp::u32>(ramEnd) == 0xDEADBEEF);

    [[maybe_unused]] Address ramMirror = 0x80000000 + 0x20;
    system.write<psxrecomp::u32>(MemoryMap::RAM_BASE + 0x20, 0x12345678);
    assert(system.read<psxrecomp::u32>(ramMirror) == 0x12345678);

    Address ramOut = 0x00800000u;
    system.write<psxrecomp::u32>(ramOut, 0xFACEB00C);
    assert(system.read<psxrecomp::u32>(ramOut) == 0);

    Address gpuBase = DmaController::ChannelBase +
                      DmaController::ChannelStride * static_cast<Address>(DmaPort::Gpu);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x04000002u);
    system.write<psxrecomp::u32>(0x00010000, 0x11111111);
    system.write<psxrecomp::u32>(0x00010004, 0x22222222);
    system.write<psxrecomp::u32>(gpuBase + 0x0, 0x00010000);
    system.write<psxrecomp::u32>(gpuBase + 0x4, 0x00000002);
    system.write<psxrecomp::u32>(gpuBase + 0x8, 0x01000001);

    assert(system.gpu().fifoDepth() == 2);
    assert(system.gpu().peekFifo() == 0x11111111);
    const auto dmaLine = static_cast<psxrecomp::u32>(InterruptLine::Dma);
    assert((system.interrupts().readStatus() & dmaLine) == 0u);

    // DMA IRQ should only assert when DICR enables that source.
    const psxrecomp::u32 dmaGpuEnable =
        (1u << (16 + static_cast<psxrecomp::u32>(DmaPort::Gpu))) | (1u << 23);
    system.writeMmioExplicit<psxrecomp::u32>(DmaController::InterruptReg, (1u << 26));
    system.writeMmioExplicit<psxrecomp::u32>(DmaController::InterruptReg, dmaGpuEnable);
    assert((system.interrupts().readStatus() & dmaLine) == 0u);
    system.write<psxrecomp::u32>(0x00010008, 0x33333333);
    system.write<psxrecomp::u32>(gpuBase + 0x0, 0x00010008);
    system.write<psxrecomp::u32>(gpuBase + 0x4, 0x00000001);
    system.write<psxrecomp::u32>(gpuBase + 0x8, 0x01000001);
    assert((system.interrupts().readStatus() & dmaLine) != 0u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::INTERRUPT_STATUS, ~dmaLine);
    assert((system.interrupts().readStatus() & dmaLine) != 0u);
    system.writeMmioExplicit<psxrecomp::u32>(DmaController::InterruptReg,
                                             dmaGpuEnable | (1u << 26));
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::INTERRUPT_STATUS, ~dmaLine);
    assert((system.interrupts().readStatus() & dmaLine) == 0u);

    // GPU DMA RAM->GPU should honor address decrement mode in normal sync.
    system.gpu().reset();
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x04000002u);
    system.write<psxrecomp::u32>(0x00013FFC, 0xBBBBBBBBu);
    system.write<psxrecomp::u32>(0x00014000, 0xAAAAAAAAu);
    system.write<psxrecomp::u32>(gpuBase + 0x0, 0x00014000);
    system.write<psxrecomp::u32>(gpuBase + 0x4, 0x00000002);
    system.write<psxrecomp::u32>(gpuBase + 0x8, 0x01000003);

    assert(system.gpu().fifoDepth() == 2);
    assert(system.gpu().peekFifo() == 0xAAAAAAAAu);

    // GPU DMA RAM->GPU request mode should transfer blockCount * wordsPerBlock words.
    system.gpu().reset();
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x04000002u);
    system.write<psxrecomp::u32>(0x00014100, 0x11111111u);
    system.write<psxrecomp::u32>(0x00014104, 0x22222222u);
    system.write<psxrecomp::u32>(gpuBase + 0x0, 0x00014100);
    system.write<psxrecomp::u32>(gpuBase + 0x4, 0x00020001);
    system.write<psxrecomp::u32>(gpuBase + 0x8, 0x01000201);
    assert(system.gpu().fifoDepth() == 2);
    assert(system.gpu().peekFifo() == 0x11111111u);
    system.gpu().tickGpu(2);
    assert(system.gpu().peekFifo() == 0x22222222u);

    system.gpu().reset();
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x04000002u);
    system.write<psxrecomp::u32>(0x00012000, 0x03800000u);
    system.write<psxrecomp::u32>(0x00012004, 0x020000FFu);
    system.write<psxrecomp::u32>(0x00012008, 0x00000000u);
    system.write<psxrecomp::u32>(0x0001200C, 0x00010001u);
    system.write<psxrecomp::u32>(gpuBase + 0x0, 0x00012000);
    system.write<psxrecomp::u32>(gpuBase + 0x4, 0x00000000);
    system.write<psxrecomp::u32>(gpuBase + 0x8, 0x01000401);
    assert(!system.gpu().commandTrace().empty());
    assert(system.gpu().commandTrace().back().kind ==
           psxrecomp::runtime::GpuCommandKind::FillRectangle);

    // Linked-list GPU DMA should wrap command read addresses in 2MB RAM window.
    system.gpu().reset();
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x04000002u);
    system.write<psxrecomp::u32>(0x001FFFFC, 0x01FFFFFFu);
    system.write<psxrecomp::u32>(0x00000000, 0x00000000u);
    system.write<psxrecomp::u32>(gpuBase + 0x0, 0x001FFFFC);
    system.write<psxrecomp::u32>(gpuBase + 0x4, 0x00000000);
    system.write<psxrecomp::u32>(gpuBase + 0x8, 0x01000401);
    assert(system.gpu().fifoDepth() == 1);
    assert(system.gpu().peekFifo() == 0x00000000u);

    // GPU DMA RAM<-GPU path should read GPUREAD words into RAM when channel direction is to RAM.
    system.gpu().reset();
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x04000003u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0xA0000000u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0x00000000u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0x00010002u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0x22221111u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x04000003u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0xC0000000u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0x00000000u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0x00010002u);

    const Address gpuReadDmaBase = 0x00013000;
    system.write<psxrecomp::u32>(gpuBase + 0x0, gpuReadDmaBase);
    system.write<psxrecomp::u32>(gpuBase + 0x4, 0x00000001);
    system.write<psxrecomp::u32>(gpuBase + 0x8, 0x01000000);
    assert(system.read<psxrecomp::u32>(gpuReadDmaBase) == 0x22221111u);

    // GPU DMA RAM<-GPU linked-list sync mode is invalid and should be ignored.
    system.write<psxrecomp::u32>(gpuReadDmaBase + 4, 0xCAFEBABEu);
    system.write<psxrecomp::u32>(gpuBase + 0x0, gpuReadDmaBase + 4);
    system.write<psxrecomp::u32>(gpuBase + 0x4, 0x00000001);
    system.write<psxrecomp::u32>(gpuBase + 0x8, 0x01000400);
    assert(system.read<psxrecomp::u32>(gpuReadDmaBase + 4) == 0xCAFEBABEu);

    // runFrame() should advance GPU FIFO consumption so GPUSTAT DMA request
    // can recover from a saturated FIFO.
    system.gpu().reset();
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x04000002u);
    while (system.gpu().fifoDepth() < 64)
    {
        system.gpu().writeDma(0x01000000u);
    }
    assert((system.gpu().readStatus() & (1u << 28)) == 0u);
    system.runFrame();
    assert(system.gpu().fifoDepth() == 0u);
    assert((system.gpu().readStatus() & (1u << 28)) != 0u);

    // GPU GP0 interrupt command should propagate to the hardware IRQ line.
    system.interrupts().restoreState(0, static_cast<psxrecomp::u32>(InterruptLine::Gpu));
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0x1F000000u);
    assert((system.interrupts().readStatus() & static_cast<psxrecomp::u32>(InterruptLine::Gpu)) !=
           0u);
    // Acknowledging I_STAT while GPU source is still active should reassert.
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::INTERRUPT_STATUS,
                                             ~static_cast<psxrecomp::u32>(InterruptLine::Gpu));
    assert((system.interrupts().readStatus() & static_cast<psxrecomp::u32>(InterruptLine::Gpu)) !=
           0u);
    // GP1 acknowledge + I_STAT acknowledge should clear and keep it cleared.
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x02000000u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::INTERRUPT_STATUS,
                                             ~static_cast<psxrecomp::u32>(InterruptLine::Gpu));
    assert((system.interrupts().readStatus() & static_cast<psxrecomp::u32>(InterruptLine::Gpu)) ==
           0u);
    // GP0(01h) cache clear should not trigger GPU IRQ.
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0x01000000u);
    system.tickCpuCycles(1);
    assert((system.interrupts().readStatus() & static_cast<psxrecomp::u32>(InterruptLine::Gpu)) ==
           0u);

    runRuntimeInterruptAndTimerChecks(system);

    std::vector<psxrecomp::u8> xaSector(2352, 0x00);
    xaSector[24] = 0x11;
    xaSector[25] = 0x22;
    xaSector[26] = 0x33;
    xaSector[27] = 0x44;
    system.cdrom().enqueueDataSector(xaSector);

    std::vector<psxrecomp::u8> xaSector2(2352, 0x00);
    xaSector2[24] = 0xAA;
    xaSector2[25] = 0xBB;
    xaSector2[26] = 0xCC;
    xaSector2[27] = 0xDD;
    system.cdrom().enqueueDataSector(xaSector2);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 1u);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 2, 0x01);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0u);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 2, 0x40);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1, 0x0E);
    ackCdromIrq(system);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1, 0x06);
    ackCdromIrq(system);
    system.runFrame();
    // Set BFRD so DMA3 can access the sector data accepted for host read.
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0u);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3, 0x80u);

    Address cdromBase =
        psxrecomp::runtime::DmaController::ChannelBase +
        psxrecomp::runtime::DmaController::ChannelStride * static_cast<Address>(DmaPort::Cdrom);
    const Address cdromDmaOut = 0x00015000;
    system.write<psxrecomp::u32>(cdromBase + 0x0, cdromDmaOut);
    system.write<psxrecomp::u32>(cdromBase + 0x4, 0x00000001);
    system.write<psxrecomp::u32>(cdromBase + 0x8, 0x01000000);
    assert(system.read<psxrecomp::u32>(cdromDmaOut) == 0x44332211u);

    // MDEC MMIO presence + DMA0/1 scaffolding should be observable even before
    // real decode output exists.
    bool sawMdecDecodeLog = false;
    system.logger().setMinLevel(psxrecomp::runtime::LogLevel::Info);
    system.logger().setCallback(
        [&sawMdecDecodeLog](const psxrecomp::runtime::LogEvent& event)
        {
            if (event.level == psxrecomp::runtime::LogLevel::Info && event.category == "mdec" &&
                event.message.find("MDEC(1)") != std::string::npos)
            {
                sawMdecDecodeLog = true;
            }
        });

    constexpr Address mdecData = psxrecomp::runtime::Mmio::MDEC_BASE;
    constexpr Address mdecStatus = psxrecomp::runtime::Mmio::MDEC_BASE + 4;
    assert((system.readMmioExplicit<psxrecomp::u32>(mdecStatus) & (1u << 31)) != 0u);

    system.writeMmioExplicit<psxrecomp::u32>(mdecStatus, (1u << 30) | (1u << 29));
    system.writeMmioExplicit<psxrecomp::u32>(mdecData, 0x38000001u);
    assert((system.readMmioExplicit<psxrecomp::u32>(mdecStatus) & (1u << 29)) != 0u);
    assert((system.readMmioExplicit<psxrecomp::u32>(mdecStatus) & (1u << 28)) != 0u);
    assert((system.readMmioExplicit<psxrecomp::u32>(mdecStatus) & (3u << 25)) == (3u << 25));
    assert((system.readMmioExplicit<psxrecomp::u32>(mdecStatus) & 0xFFFFu) == 0u);

    const Address mdecInBase = DmaController::ChannelBase +
                               DmaController::ChannelStride * static_cast<Address>(DmaPort::MdecIn);
    const Address mdecOutBase =
        DmaController::ChannelBase +
        DmaController::ChannelStride * static_cast<Address>(DmaPort::MdecOut);
    constexpr Address mdecParamSource = 0x00017000u;
    constexpr Address mdecOutputDest = 0x00017020u;
    system.write<psxrecomp::u32>(mdecParamSource, 0xFE00FE00u);
    system.write<psxrecomp::u32>(mdecInBase + 0x0, mdecParamSource);
    system.write<psxrecomp::u32>(mdecInBase + 0x4, 0x00010001u);
    system.write<psxrecomp::u32>(mdecInBase + 0x8, 0x01000201u);
    assert(sawMdecDecodeLog);

    assert((system.readMmioExplicit<psxrecomp::u32>(mdecStatus) & (1u << 29)) == 0u);
    assert((system.readMmioExplicit<psxrecomp::u32>(mdecStatus) & (1u << 27)) != 0u);
    assert((system.readMmioExplicit<psxrecomp::u32>(mdecStatus) & (1u << 31)) == 0u);

    system.write<psxrecomp::u32>(mdecOutputDest + 0x0, 0xFFFFFFFFu);
    system.write<psxrecomp::u32>(mdecOutputDest + 0x4, 0xFFFFFFFFu);
    system.write<psxrecomp::u32>(mdecOutBase + 0x0, mdecOutputDest);
    system.write<psxrecomp::u32>(mdecOutBase + 0x4, 0x00010002u);
    system.write<psxrecomp::u32>(mdecOutBase + 0x8, 0x01000200u);
    assert(system.read<psxrecomp::u32>(mdecOutputDest + 0x0) == 0u);
    assert(system.read<psxrecomp::u32>(mdecOutputDest + 0x4) == 0u);
    assert(system.mdec().lastDmaWord() == 0u);
    system.logger().setCallback({});

    // A second frame should queue another sector without dropping boundaries.
    // Per the per-sector host-visible protocol, xaSector2 is only accessible
    // after explicitly: clearing BFRD, acknowledging xaSector's INT1 (which
    // promotes INT1 for xaSector2 and moves it to m_activeSector), then
    // re-arming BFRD (0→1) to load xaSector2 into the data FIFO.
    system.runFrame();
    // Clear BFRD (falling edge discards current FIFO contents).
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0u);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3, 0x00u);
    // Ack xaSector's INT1 → INT1 for xaSector2 fires, xaSector2 → m_activeSector.
    ackCdromIrq(system);
    // Re-arm BFRD (0→1) → xaSector2 loaded into data FIFO.
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0u);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3, 0x80u);
    [[maybe_unused]] bool reachedSecondSector = (system.cdrom().readDma() == 0xDDCCBBAAu);
    assert(reachedSecondSector);

    // CD-ROM queued sectors are bounded to avoid unbounded memory growth.
    PsxSystem boundedQueueSystem;
    assert(boundedQueueSystem.initialize());
    for (int i = 0; i < 80; ++i)
    {
        std::vector<psxrecomp::u8> sector(2048, 0);
        sector[0] = static_cast<psxrecomp::u8>(i);
        boundedQueueSystem.cdrom().enqueueDataSector(sector);
    }
    boundedQueueSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0,
                                                        1u);
    boundedQueueSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 2,
                                                        0x01);
    boundedQueueSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0,
                                                        0u);
    boundedQueueSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1,
                                                        0x06);
    // Ack the INT3 command-response from ReadN, then tick one full read
    // interval so the first sector INT1 fires.  publishNextInterruptEvent()
    // then moves sector 16 (the first bounded one) into m_activeSector.
    ackCdromIrq(boundedQueueSystem);
    boundedQueueSystem.tickCpuCycles(451584u);
    // Set BFRD (0→1): loads m_activeSector (sector 16) into the data FIFO.
    boundedQueueSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0,
                                                        0u);
    boundedQueueSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3,
                                                        0x80u);
    [[maybe_unused]] const psxrecomp::u32 boundedWord = boundedQueueSystem.cdrom().readDma();
    assert((boundedWord & 0xFFu) == 16u);

    // CD-ROM DMA loops without CPU data-port reads should preserve sector boundaries.
    PsxSystem dmaLoopSystem;
    assert(dmaLoopSystem.initialize());
    for (psxrecomp::u32 sectorIndex = 0; sectorIndex < 3; ++sectorIndex)
    {
        std::vector<psxrecomp::u8> sector(2048, 0);
        for (psxrecomp::u32 wordIndex = 0; wordIndex < 512; ++wordIndex)
        {
            const psxrecomp::u32 value = 0xA0000000u | (sectorIndex << 16) | wordIndex;
            const size_t base = static_cast<size_t>(wordIndex) * sizeof(psxrecomp::u32);
            sector[base + 0] = static_cast<psxrecomp::u8>(value & 0xFFu);
            sector[base + 1] = static_cast<psxrecomp::u8>((value >> 8) & 0xFFu);
            sector[base + 2] = static_cast<psxrecomp::u8>((value >> 16) & 0xFFu);
            sector[base + 3] = static_cast<psxrecomp::u8>((value >> 24) & 0xFFu);
        }
        dmaLoopSystem.cdrom().enqueueDataSector(sector);
    }

    dmaLoopSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0u);
    dmaLoopSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 2, 0x00);
    dmaLoopSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 2, 0x02);
    dmaLoopSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 2, 0x00);
    dmaLoopSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1, 0x02);
    ackCdromIrq(dmaLoopSystem);
    dmaLoopSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1, 0x06);
    ackCdromIrq(dmaLoopSystem);
    // Tick enough cycles to buffer all three sectors and fire INT1 for the
    // first one.  The remaining two sectors stay in m_bufferedReadSectors and
    // will become available after each sector's INT1 is acknowledged and BFRD
    // is re-armed per the per-sector host-visible data-ready protocol.
    dmaLoopSystem.tickCpuCycles(451584u * 3u); // CDROM_READ_CYCLES * 3

    const Address dmaLoopCdromBase =
        psxrecomp::runtime::DmaController::ChannelBase +
        psxrecomp::runtime::DmaController::ChannelStride * static_cast<Address>(DmaPort::Cdrom);
    constexpr Address dmaLoopOutBase = 0x00018000;
    constexpr psxrecomp::u32 dmaWordsPerChunk = 0x100u; // 1024 bytes/chunk
    constexpr psxrecomp::u32 bytesPerChunk = dmaWordsPerChunk * sizeof(psxrecomp::u32);
    constexpr psxrecomp::u32 chunksPerSector = 2u; // 2 × 256 words = 2048 bytes = 1 sector

    // For each sector: BFRD=1 (0→1) → 2 DMA chunks → BFRD=0 → ack INT1.
    // After the ack, publishNextInterruptEvent fires the next INT1 and moves
    // the following sector into m_activeSector, ready for the next BFRD re-arm.
    for (psxrecomp::u32 s = 0; s < 3u; ++s)
    {
        // Arm BFRD (0→1): loads the current m_activeSector into the data FIFO.
        dmaLoopSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0,
                                                       0u);
        dmaLoopSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3,
                                                       0x80u);
        for (psxrecomp::u32 c = 0; c < chunksPerSector; ++c)
        {
            const psxrecomp::u32 chunk = s * chunksPerSector + c;
            const Address chunkDest = dmaLoopOutBase + static_cast<Address>(chunk * bytesPerChunk);
            dmaLoopSystem.write<psxrecomp::u32>(dmaLoopCdromBase + 0x0,
                                                static_cast<psxrecomp::u32>(chunkDest));
            dmaLoopSystem.write<psxrecomp::u32>(dmaLoopCdromBase + 0x4, dmaWordsPerChunk);
            dmaLoopSystem.write<psxrecomp::u32>(dmaLoopCdromBase + 0x8, 0x01000000u);
        }
        // Clear BFRD (1→0): ensures the next BFRD write is a genuine 0→1 edge.
        dmaLoopSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0,
                                                       0u);
        dmaLoopSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3,
                                                       0x00u);
        // Ack INT1 for this sector: publishNextInterruptEvent fires, the next
        // sector (if any) moves from m_bufferedReadSectors to m_activeSector.
        ackCdromIrq(dmaLoopSystem);
    }

    for (psxrecomp::u32 chunk = 0; chunk < 6; ++chunk)
    {
        [[maybe_unused]] const Address chunkDest =
            dmaLoopOutBase + static_cast<Address>(chunk * bytesPerChunk);
        const psxrecomp::u32 firstWordGlobal = chunk * dmaWordsPerChunk;
        const psxrecomp::u32 firstSector = firstWordGlobal / 512u;
        const psxrecomp::u32 firstWordInSector = firstWordGlobal % 512u;
        [[maybe_unused]] const psxrecomp::u32 expectedFirst =
            0xA0000000u | (firstSector << 16) | firstWordInSector;
        assert(dmaLoopSystem.read<psxrecomp::u32>(chunkDest) == expectedFirst);

        const psxrecomp::u32 lastWordGlobal = firstWordGlobal + (dmaWordsPerChunk - 1u);
        const psxrecomp::u32 lastSector = lastWordGlobal / 512u;
        const psxrecomp::u32 lastWordInSector = lastWordGlobal % 512u;
        [[maybe_unused]] const psxrecomp::u32 expectedLast =
            0xA0000000u | (lastSector << 16) | lastWordInSector;
        assert(dmaLoopSystem.read<psxrecomp::u32>(chunkDest + bytesPerChunk -
                                                  sizeof(psxrecomp::u32)) == expectedLast);
    }

    bool fired = false;
    system.scheduler().schedule(5, [&fired]() { fired = true; });
    system.scheduler().tick(4);
    assert(!fired);
    system.scheduler().tick(1);
    assert(fired);

    // Scheduler must process events in chronological order and execute
    // events scheduled by callbacks inside the same tick window.
    psxrecomp::runtime::Scheduler orderedScheduler;
    std::vector<int> order;
    orderedScheduler.schedule(7, [&order]() { order.push_back(7); });
    orderedScheduler.schedule(3,
                              [&order, &orderedScheduler]()
                              {
                                  order.push_back(3);
                                  orderedScheduler.schedule(2, [&order]() { order.push_back(5); });
                              });
    orderedScheduler.schedule(10, [&order]() { order.push_back(10); });
    orderedScheduler.tick(10);
    assert((order == std::vector<int>{3, 5, 7, 10}));
    assert(orderedScheduler.now() == 10);

    // MMIO polling must not advance emulated time on its own.
    PsxSystem pollOnlySystem;
    assert(pollOnlySystem.initialize());
    for (int i = 0; i < 5000; ++i)
    {
        (void)pollOnlySystem.readMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1);
        (void)pollOnlySystem.readMmioExplicit<psxrecomp::u32>(
            psxrecomp::runtime::Mmio::INTERRUPT_STATUS);
    }
    assert(pollOnlySystem.frameCount() == 0);
    assert(pollOnlySystem.cpuCyclesElapsed() == 0);
    pollOnlySystem.tickCpuCycles(564480);
    assert(pollOnlySystem.frameCount() == 1);
    assert(pollOnlySystem.cpuCyclesElapsed() == 564480);

    runRuntimeLoggingAndDumpChecks(system);
    runRuntimeStateSerializationChecks(system);
    runRuntimeResourcePackChecks();

    return 0;
}
