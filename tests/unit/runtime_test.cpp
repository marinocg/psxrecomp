#include "psxrecomp/runtime/psx_system.h"
#include "psxrecomp/runtime/resource_pack.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>

namespace MemoryMap = psxrecomp::MemoryMap;

int main()
{
    using psxrecomp::Address;
    using psxrecomp::runtime::DmaPort;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::LogLevel;
    using psxrecomp::runtime::PsxSystem;
    using psxrecomp::runtime::ResourcePack;

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

    Address gpuBase =
        psxrecomp::runtime::DmaController::ChannelBase +
        psxrecomp::runtime::DmaController::ChannelStride * static_cast<Address>(DmaPort::Gpu);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x04000002u);
    system.write<psxrecomp::u32>(0x00010000, 0x11111111);
    system.write<psxrecomp::u32>(0x00010004, 0x22222222);
    system.write<psxrecomp::u32>(gpuBase + 0x0, 0x00010000);
    system.write<psxrecomp::u32>(gpuBase + 0x4, 0x00000002);
    system.write<psxrecomp::u32>(gpuBase + 0x8, 0x01000001);

    assert(system.gpu().fifoDepth() == 2);
    assert(system.gpu().peekFifo() == 0x11111111);
    assert((system.interrupts().readStatus() & static_cast<psxrecomp::u32>(InterruptLine::Dma)) !=
           0);

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

    Address spuBase =
        psxrecomp::runtime::DmaController::ChannelBase +
        psxrecomp::runtime::DmaController::ChannelStride * static_cast<Address>(DmaPort::Spu);
    system.write<psxrecomp::u32>(0x00011000, 0xABCDEF01);
    system.write<psxrecomp::u32>(spuBase + 0x0, 0x00011000);
    system.write<psxrecomp::u32>(spuBase + 0x4, 0x00000001);
    system.write<psxrecomp::u32>(spuBase + 0x8, 0x01000001);

    assert(system.spu().lastDmaWord() == 0xABCDEF01);

    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0x19);
    assert(system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1) ==
           0x19);
    assert(system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1) ==
           0x00);

    // Timer0 target IRQ should fire once runFrame advances cycles.
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::INTERRUPT_MASK,
                                             static_cast<psxrecomp::u32>(InterruptLine::Timer0));
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x8, 3u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4, 0x0018u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0, 0u);
    assert((system.interrupts().readStatus() &
            static_cast<psxrecomp::u32>(InterruptLine::Timer0)) == 0);
    system.runFrame();
    assert((system.interrupts().readStatus() &
            static_cast<psxrecomp::u32>(InterruptLine::Timer0)) != 0);

    const psxrecomp::u16 timer0Mode =
        system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4);
    assert((timer0Mode & (1u << 11)) != 0);

    // Timer2 alternate divider mode should only advance every 8 CPU cycles.
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x20 + 0x4,
                                             0x0200u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x20 + 0x0, 0u);
    system.timers().tick(7, nullptr);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x20 +
                                                   0x0) == 0u);
    system.timers().tick(1, nullptr);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x20 +
                                                   0x0) == 1u);

    // Timer overflow should wrap counter in free-running mode.
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4, 0x0000u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0, 0xFFFEu);
    system.timers().tick(4, nullptr);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0) ==
           2u);

    // Reset-on-target with target=0 should behave as 0x10000 period (no divide-by-zero).
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4, 0x0008u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x8, 0x0000u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0, 0xFFFEu);
    system.timers().tick(4, nullptr);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0) ==
           2u);

    // Reset-on-target should not trigger early when counter starts above target.
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4, 0x0018u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x8, 3u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0, 10u);
    system.timers().tick(3, nullptr);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0) ==
           13u);

    // Target flag must not latch on overflow if target value was not crossed.
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4, 0x0000u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x8, 0x7FFFu);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0, 0xFFFEu);
    system.timers().tick(2, nullptr);
    const psxrecomp::u16 noTargetOnOverflowMode =
        system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4);
    assert((noTargetOnOverflowMode & (1u << 11)) == 0u);

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

    bool sawInfo = false;
    system.logger().setMinLevel(LogLevel::Info);
    system.logger().setCallback(
        [&sawInfo](const psxrecomp::runtime::LogEvent& event)
        {
            if (event.level == LogLevel::Info && event.category == "bios")
            {
                sawInfo = true;
            }
        });
    const psxrecomp::u32 regs[32] = {'A'};
    system.callBiosSyscall(0x3F, regs, 32);
    assert(sawInfo);

    system.callBiosSyscall(0x3F, regs, 1);
    auto lastEvent = system.logger().lastEvent();
    assert(lastEvent.has_value());
    assert(lastEvent->level == LogLevel::Warn);

    system.runFrame();
    assert(system.debugOverlay().frameCounter() >= 1);
    assert(system.debugOverlay().dmaTransfers() >= 2);

    auto ramDump = system.dumpRam();
    auto vramDump = system.dumpVram();
    auto spuDump = system.dumpSpuRam();
    assert(ramDump.size() == MemoryMap::RAM_SIZE);
    assert(!vramDump.empty());
    assert(!spuDump.empty());

    auto checksum1 = system.stateChecksum();
    auto state = system.serializeState();

    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0xAABBCCDDu);
    assert(system.gpu().fifoDepth() > 0);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0x1Au);
    assert((system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0) &
            0x1Fu) == 0x1Au);

    assert(system.deserializeState(state));
    auto checksum2 = system.stateChecksum();
    assert(checksum1 == checksum2);
    assert(system.gpu().fifoDepth() == 0);
    assert(system.readMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0) == 0);
    assert(system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0) == 0);

    auto stateWithInterrupts = state;
    const size_t interruptStateOffset = sizeof(psxrecomp::u32) + MemoryMap::RAM_SIZE +
                                        sizeof(psxrecomp::u32) + MemoryMap::SCRATCHPAD_SIZE +
                                        sizeof(psxrecomp::u32) + MemoryMap::BIOS_SIZE;
    stateWithInterrupts[interruptStateOffset + 0] = 0xAA;
    stateWithInterrupts[interruptStateOffset + 1] = 0x55;
    stateWithInterrupts[interruptStateOffset + 2] = 0x00;
    stateWithInterrupts[interruptStateOffset + 3] = 0xF0;
    stateWithInterrupts[interruptStateOffset + 4] = 0x0F;
    stateWithInterrupts[interruptStateOffset + 5] = 0x00;
    stateWithInterrupts[interruptStateOffset + 6] = 0x00;
    stateWithInterrupts[interruptStateOffset + 7] = 0x00;
    assert(system.deserializeState(stateWithInterrupts));
    assert(system.interrupts().readStatus() == 0xF00055AAu);
    assert(system.interrupts().readMask() == 0x0000000Fu);

    auto stateWithJunk = state;
    const auto firstRamByteBeforeFailedLoad = system.read<psxrecomp::u8>(MemoryMap::RAM_BASE);
    stateWithJunk[4] = static_cast<psxrecomp::u8>(firstRamByteBeforeFailedLoad ^ 0xFFu);
    stateWithJunk.push_back(0x99);
    assert(!system.deserializeState(stateWithJunk));
    assert(system.read<psxrecomp::u8>(MemoryMap::RAM_BASE) == firstRamByteBeforeFailedLoad);

    auto tempRoot = std::filesystem::temp_directory_path() / "psxrecomp_runtime_test_assets";
    std::filesystem::create_directories(tempRoot / "textures");
    {
        std::ofstream file(tempRoot / "textures" / "logo.bin", std::ios::binary);
        const char bytes[] = {1, 2, 3, 4};
        file.write(bytes, sizeof(bytes));
    }

    ResourcePack pack;
    assert(!pack.loadFromDirectory(tempRoot / "missing"));
    assert(pack.loadFromDirectory(tempRoot));
    assert(pack.hasResource("textures/logo.bin"));
    auto resource = pack.readResource("textures/logo.bin");
    assert(resource.has_value());
    assert(resource->size() == 4);
    assert(pack.resourceCount() == 1);

    std::filesystem::remove(tempRoot / "textures" / "logo.bin");
    auto missingResource = pack.readResource("textures/logo.bin");
    assert(!missingResource.has_value());

    std::filesystem::remove_all(tempRoot);

    return 0;
}
