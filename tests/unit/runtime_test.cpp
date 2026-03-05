#include "psxrecomp/runtime/psx_system.h"
#include "runtime_test_sections.h"

#include <cassert>
#include <vector>

namespace MemoryMap = psxrecomp::MemoryMap;

int main()
{
    using psxrecomp::Address;
    using psxrecomp::runtime::DmaController;
    using psxrecomp::runtime::DmaPort;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    assert(system.initialize());

    Address ramEnd = MemoryMap::RAM_BASE + MemoryMap::RAM_SIZE - sizeof(psxrecomp::u32);
    system.write<psxrecomp::u32>(ramEnd, 0xDEADBEEF);
    assert(system.read<psxrecomp::u32>(ramEnd) == 0xDEADBEEF);

    Address ramMirror = 0x80000000 + 0x20;
    system.write<psxrecomp::u32>(MemoryMap::RAM_BASE + 0x20, 0x12345678);
    assert(system.read<psxrecomp::u32>(ramMirror) == 0x12345678);

    Address ramOut = MemoryMap::RAM_BASE + MemoryMap::RAM_SIZE;
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
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3, 0x01);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1, 0x40);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0x0E);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0x06);
    system.runFrame();

    Address cdromBase =
        psxrecomp::runtime::DmaController::ChannelBase +
        psxrecomp::runtime::DmaController::ChannelStride * static_cast<Address>(DmaPort::Cdrom);
    const Address cdromDmaOut = 0x00015000;
    system.write<psxrecomp::u32>(cdromBase + 0x0, cdromDmaOut);
    system.write<psxrecomp::u32>(cdromBase + 0x4, 0x00000001);
    system.write<psxrecomp::u32>(cdromBase + 0x8, 0x01000000);
    assert(system.read<psxrecomp::u32>(cdromDmaOut) == 0x44332211u);

    // A second frame should queue another sector without dropping bytes when FIFO fills.
    system.runFrame();
    for (int i = 0; i < 581; ++i)
    {
        (void)system.cdrom().readDma();
    }
    assert(system.cdrom().readDma() == 0xDDCCBBAAu);

    // CD-ROM queued sectors are bounded to avoid unbounded memory growth.
    PsxSystem boundedQueueSystem;
    assert(boundedQueueSystem.initialize());
    for (int i = 0; i < 80; ++i)
    {
        std::vector<psxrecomp::u8> sector(2048, 0);
        sector[0] = static_cast<psxrecomp::u8>(i);
        boundedQueueSystem.cdrom().enqueueDataSector(sector);
    }
    boundedQueueSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3,
                                                        0x01);
    boundedQueueSystem.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0,
                                                        0x06);
    boundedQueueSystem.runFrame();
    const psxrecomp::u32 boundedWord = boundedQueueSystem.cdrom().readDma();
    assert((boundedWord & 0xFFu) == 16u);

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
