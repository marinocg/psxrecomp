#include "psxrecomp/runtime/psx_system.h"

#include <cassert>

namespace MemoryMap = psxrecomp::MemoryMap;

int main()
{
    using psxrecomp::Address;
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

    bool fired = false;
    system.scheduler().schedule(5, [&fired]() { fired = true; });
    system.scheduler().tick(4);
    assert(!fired);
    system.scheduler().tick(1);
    assert(fired);

    return 0;
}
