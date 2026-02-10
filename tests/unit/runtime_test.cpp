#include "psxrecomp/runtime/psx_system.h"
#include "psxrecomp/runtime/resource_pack.h"

#include <cassert>
#include <filesystem>
#include <fstream>

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
    system.write<psxrecomp::u32>(0x00010000, 0x11111111);
    system.write<psxrecomp::u32>(0x00010004, 0x22222222);
    system.write<psxrecomp::u32>(gpuBase + 0x0, 0x00010000);
    system.write<psxrecomp::u32>(gpuBase + 0x4, 0x00000002);
    system.write<psxrecomp::u32>(gpuBase + 0x8, 0x01000001);

    assert(system.gpu().fifoDepth() == 2);
    assert(system.gpu().peekFifo() == 0x11111111);
    assert((system.interrupts().readStatus() & static_cast<psxrecomp::u32>(InterruptLine::Dma)) !=
           0);

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
    assert(system.debugOverlay().frameCounter() == 1);
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

    assert(system.deserializeState(state));
    auto checksum2 = system.stateChecksum();
    assert(checksum1 == checksum2);
    assert(system.gpu().fifoDepth() == 0);
    assert(system.readMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0) == 0);

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
