#include "psxrecomp/runtime/gpu.h"

#include <cassert>

namespace
{
using psxrecomp::u32;
using psxrecomp::runtime::Gpu;

constexpr u32 StatusReadyDmaBlock = 1u << 28;
}

int main()
{
    Gpu gpu;
    gpu.reset();

    // GP1(04h) affects GPUSTAT request semantics, but DMA2 RAM->GPU payloads
    // still arrive on GP0. SDK font/image upload paths rely on this.
    gpu.writeCommand(0xA0000000u);
    gpu.writeCommand(0x00000000u);
    gpu.writeCommand(0x00010002u);
    assert((gpu.readStatus() & StatusReadyDmaBlock) != 0u);

    gpu.writeDma(0x22221111u);
    assert((gpu.readStatus() & StatusReadyDmaBlock) != 0u);

    gpu.writeCommand(0xC0000000u);
    gpu.writeCommand(0x00000000u);
    gpu.writeCommand(0x00010002u);
    assert(gpu.readData() == 0x22221111u);

    return 0;
}
