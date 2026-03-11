#include "psxrecomp/runtime/gpu.h"

#include <cassert>

namespace
{
using psxrecomp::runtime::Gpu;

constexpr psxrecomp::u32 StatusDmaDataRequest = 1u << 25;
constexpr psxrecomp::u32 StatusReadyToReceiveCommand = 1u << 26;
constexpr psxrecomp::u32 StatusReadyToSendToCpu = 1u << 27;
constexpr psxrecomp::u32 StatusReadyToReceiveDmaBlock = 1u << 28;
constexpr psxrecomp::u32 StatusDisplayDisable = 1u << 23;
constexpr psxrecomp::u32 StatusInterlaceGate = 1u << 19;
constexpr psxrecomp::u32 StatusVerticalInterlace = 1u << 22;
constexpr psxrecomp::u32 StatusFieldBit = 1u << 31;
} // namespace

int main()
{
    Gpu gpu;
    gpu.reset();

    while (gpu.fifoDepth() < 64)
    {
        gpu.writeCommand(0x00000000u);
    }
    const auto statusWhenFull = gpu.readStatus();
    assert((statusWhenFull & StatusReadyToReceiveCommand) == 0);

    const auto traceBeforeOverflowAttempt = gpu.commandTrace().size();
    gpu.writeCommand(0x020000FFu);
    gpu.writeCommand(0x00000000u);
    gpu.writeCommand(0x00100010u);
    assert(gpu.fifoDepth() == 64);
    assert(gpu.commandTrace().size() == traceBeforeOverflowAttempt);

    const auto initialDepth = gpu.fifoDepth();
    gpu.tickGpu(2);
    assert(gpu.fifoDepth() <= initialDepth);

    gpu.reset();
    const auto fifoDepthBeforeOffDma = gpu.fifoDepth();
    gpu.writeDma(0x12345678u);
    assert(gpu.fifoDepth() == fifoDepthBeforeOffDma);

    gpu.writeStatus(0x04000002u);
    const auto statusCpuToGp0 = gpu.readStatus();
    assert(((statusCpuToGp0 >> 29) & 0x3u) == 0x2u);
    assert((statusCpuToGp0 & StatusReadyToReceiveDmaBlock) != 0);
    assert((statusCpuToGp0 & StatusDmaDataRequest) != 0);

    gpu.writeDma(0xAABBCCDDu);
    assert(gpu.fifoDepth() == fifoDepthBeforeOffDma + 1);
    assert(gpu.readData() == 0);

    gpu.writeStatus(0x01000000u);
    assert(gpu.fifoDepth() == 0);

    gpu.writeStatus(0x04000003u);
    const auto statusGpuToCpu = gpu.readStatus();
    assert(((statusGpuToCpu >> 29) & 0x3u) == 0x3u);
    assert((statusGpuToCpu & StatusReadyToSendToCpu) != 0);
    assert((statusGpuToCpu & StatusDmaDataRequest) != 0);

    gpu.writeStatus(0x04000001u);
    assert((gpu.readStatus() & StatusDmaDataRequest) != 0);

    gpu.writeStatus(0x04000000u);
    assert((gpu.readStatus() & StatusDmaDataRequest) == 0);

    // GPUREAD readiness bit should drop while CPU->VRAM payload transfer is active.
    gpu.writeStatus(0x04000003u);
    gpu.writeCommand(0xA0000000u);
    gpu.writeCommand(0x00000000u);
    gpu.writeCommand(0x00010002u);
    assert((gpu.readStatus() & StatusReadyToSendToCpu) == 0);
    gpu.writeCommand(0xAAAABBBBu);

    gpu.writeStatus(0x03000001u);
    assert((gpu.readStatus() & StatusDisplayDisable) != 0);

    gpu.reset();
    const auto statusAfterReset = gpu.readStatus();
    assert((statusAfterReset & StatusReadyToReceiveCommand) != 0);
    assert((statusAfterReset & StatusReadyToReceiveDmaBlock) != 0);
    assert((statusAfterReset & StatusInterlaceGate) == 0);

    gpu.writeStatus(0x08000024u); // 320x480 interlaced mode
    const auto statusAfterDisplayModeWrite = gpu.readStatus();
    assert((statusAfterDisplayModeWrite & StatusInterlaceGate) != 0);
    assert((statusAfterDisplayModeWrite & StatusReadyToReceiveDmaBlock) != 0);
    gpu.tickGpu(2);
    const auto statusAfterDisplayModeSettled = gpu.readStatus();
    assert((statusAfterDisplayModeSettled & StatusInterlaceGate) != 0);
    assert((statusAfterDisplayModeSettled & StatusReadyToReceiveCommand) != 0);

    gpu.writeStatus(0x03000001u);
    assert((gpu.readStatus() & StatusDisplayDisable) != 0);
    gpu.tickGpu(2);
    gpu.writeStatus(0x03000000u);
    assert((gpu.readStatus() & StatusDisplayDisable) == 0);

    gpu.writeStatus(0x04000002u);
    const auto statusAfterDmaEnable = gpu.readStatus();
    assert(((statusAfterDmaEnable >> 29) & 0x3u) == 0x2u);
    assert((statusAfterDmaEnable & StatusDmaDataRequest) != 0);
    assert((statusAfterDmaEnable & StatusReadyToReceiveDmaBlock) != 0);
    gpu.tickGpu(2);
    gpu.writeStatus(0x04000000u);
    const auto statusAfterDmaDisable = gpu.readStatus();
    assert(((statusAfterDmaDisable >> 29) & 0x3u) == 0x0u);
    assert((statusAfterDmaDisable & StatusDmaDataRequest) == 0);

    gpu.tickGpu(2);
    const auto statusWithEmptyQueue = gpu.readStatus();
    assert((statusWithEmptyQueue & StatusReadyToReceiveCommand) != 0);
    gpu.writeCommand(0x00000000u);
    const auto statusWithQueuedCommand = gpu.readStatus();
    assert((statusWithQueuedCommand & StatusReadyToReceiveCommand) == 0);
    assert((statusWithQueuedCommand & StatusReadyToReceiveDmaBlock) != 0);
    gpu.tickGpu(2);
    const auto statusAfterQueueDrain = gpu.readStatus();
    assert((statusAfterQueueDrain & StatusReadyToReceiveCommand) != 0);

    gpu.reset();
    gpu.writeCommand(0x20000000u);
    const auto polygonStatus = gpu.readStatus();
    assert((polygonStatus & StatusReadyToReceiveCommand) == 0);
    assert((polygonStatus & StatusReadyToReceiveDmaBlock) == 0);

    gpu.reset();
    gpu.writeCommand(0x60000000u);
    const auto spriteStatus = gpu.readStatus();
    assert((spriteStatus & StatusReadyToReceiveCommand) == 0);
    assert((spriteStatus & StatusReadyToReceiveDmaBlock) != 0);

    gpu.writeStatus(0x08000020u); // interlaced 240-line mode
    const auto beforeLineTick = gpu.readStatus();
    assert((beforeLineTick & StatusVerticalInterlace) != 0);
    gpu.tickDisplayLine();
    gpu.tickDisplayLine();
    const auto afterLineTick = gpu.readStatus();
    assert((beforeLineTick & StatusFieldBit) == (afterLineTick & StatusFieldBit));

    gpu.writeStatus(0x00000000u);
    assert((gpu.readStatus() & StatusFieldBit) == 0);

    gpu.reset();
    gpu.writeStatus(0x08000000u); // progressive 240-line mode
    const auto progressiveBeforeTick = gpu.readStatus();
    assert((progressiveBeforeTick & StatusVerticalInterlace) == 0);
    gpu.tickDisplayLine();
    const auto progressiveAfterTick = gpu.readStatus();
    assert(((progressiveBeforeTick ^ progressiveAfterTick) & StatusFieldBit) != 0);

    return 0;
}
