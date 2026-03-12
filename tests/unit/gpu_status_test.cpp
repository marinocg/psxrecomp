#include "psxrecomp/runtime/gpu.h"

#include <cassert>

namespace
{
using psxrecomp::u32;
using psxrecomp::runtime::Gpu;

constexpr u32 StatusDmaRequest = 1u << 25;
constexpr u32 StatusReadyCommand = 1u << 26;
constexpr u32 StatusReadyVramToCpu = 1u << 27;
constexpr u32 StatusReadyDmaBlock = 1u << 28;
constexpr u32 StatusDisplayDisable = 1u << 23;
constexpr u32 StatusFieldOrAlwaysOne = 1u << 13;
constexpr u32 StatusReverseFlag = 1u << 14;
constexpr u32 StatusVerticalInterlace = 1u << 22;
constexpr u32 StatusFieldBit = 1u << 31;
constexpr u32 StatusDmaDirectionShift = 29;
constexpr u32 ResetStatus = 0x14802000u;
} // namespace

int main()
{
    Gpu gpu;
    gpu.reset();

    assert(gpu.readStatus() == ResetStatus);

    gpu.writeCommand(0xE2001357u);
    gpu.writeStatus(0x10000002u);
    assert(gpu.readData() == 0x00001357u);
    assert((gpu.readStatus() & StatusReadyVramToCpu) == 0);

    gpu.writeCommand(0xE3002468u);
    gpu.writeStatus(0x10000003u);
    assert(gpu.readData() == 0x00002468u);

    gpu.writeCommand(0xE400369Cu);
    gpu.writeStatus(0x10000004u);
    assert(gpu.readData() == 0x0000369Cu);

    gpu.writeCommand(0xE5001ABCu);
    gpu.writeStatus(0x10000005u);
    assert(gpu.readData() == 0x00001ABCu);

    gpu.writeStatus(0x10000007u);
    assert(gpu.readData() == 0x00000002u);
    gpu.writeStatus(0x10000008u);
    assert(gpu.readData() == 0x00000000u);

    const u32 latchBeforeUnsupported = gpu.readData();
    gpu.writeStatus(0x10000009u);
    assert(gpu.readData() == latchBeforeUnsupported);

    gpu.writeStatus(0x04000001u);
    u32 status = gpu.readStatus();
    assert(((status >> StatusDmaDirectionShift) & 0x3u) == 0x1u);
    assert((status & StatusDmaRequest) != 0);

    while (gpu.fifoDepth() < 64)
    {
        gpu.writeCommand(0x00000000u);
    }
    status = gpu.readStatus();
    assert((status & StatusDmaRequest) == 0);
    assert((status & StatusReadyCommand) == 0);

    gpu.reset();
    gpu.writeStatus(0x04000002u);
    status = gpu.readStatus();
    assert(((status >> StatusDmaDirectionShift) & 0x3u) == 0x2u);
    assert((status & StatusDmaRequest) != 0);
    assert((status & StatusReadyDmaBlock) != 0);

    gpu.writeCommand(0xA0000000u);
    gpu.writeCommand(0x00000000u);
    gpu.writeCommand(0x00010002u);
    status = gpu.readStatus();
    assert((status & StatusReadyCommand) == 0);
    assert((status & StatusReadyDmaBlock) != 0);
    assert((status & StatusDmaRequest) != 0);
    gpu.writeCommand(0xAAAABBBBu);
    status = gpu.readStatus();
    assert((status & StatusReadyCommand) != 0);
    assert((status & StatusReadyDmaBlock) != 0);
    assert((status & StatusDmaRequest) != 0);

    gpu.writeStatus(0x04000003u);
    status = gpu.readStatus();
    assert(((status >> StatusDmaDirectionShift) & 0x3u) == 0x3u);
    assert((status & StatusReadyVramToCpu) == 0);
    assert((status & StatusDmaRequest) == 0);

    gpu.writeCommand(0xC0000000u);
    gpu.writeCommand(0x00000000u);
    gpu.writeCommand(0x00010002u);
    status = gpu.readStatus();
    assert((status & StatusReadyVramToCpu) != 0);
    assert((status & StatusDmaRequest) != 0);
    assert(gpu.readData() == 0xAAAABBBBu);
    status = gpu.readStatus();
    assert((status & StatusReadyVramToCpu) == 0);
    assert((status & StatusDmaRequest) == 0);

    gpu.writeStatus(0x03000001u);
    assert((gpu.readStatus() & StatusDisplayDisable) != 0);

    gpu.writeStatus(0x08000024u);
    status = gpu.readStatus();
    assert((status & StatusVerticalInterlace) != 0);
    assert((status & StatusReadyCommand) != 0);
    assert((status & StatusReadyDmaBlock) != 0);
    assert((status & StatusFieldOrAlwaysOne) == 0);

    gpu.reset();
    assert((gpu.readStatus() & StatusFieldOrAlwaysOne) != 0);
    gpu.writeStatus(0x08000080u);
    assert((gpu.readStatus() & StatusReverseFlag) != 0);

    const u32 fieldBeforeTick = gpu.readStatus() & StatusFieldBit;
    gpu.tickDisplayLine();
    gpu.tickDisplayLine();
    const u32 fieldAfterTick = gpu.readStatus() & StatusFieldBit;
    assert(fieldBeforeTick == fieldAfterTick);

    gpu.reset();
    const u32 progressiveBeforeTick = gpu.readStatus() & StatusFieldBit;
    gpu.tickDisplayLine();
    const u32 progressiveAfterTick = gpu.readStatus() & StatusFieldBit;
    assert(progressiveBeforeTick != progressiveAfterTick);

    return 0;
}
