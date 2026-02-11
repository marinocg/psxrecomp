#include "psxrecomp/runtime/gpu.h"

#include <cassert>

int main()
{
    using psxrecomp::runtime::Gpu;

    Gpu gpu;
    gpu.reset();

    gpu.writeCommand(0x020000FFu);
    gpu.writeCommand(0x00000000u);
    gpu.writeCommand(0x00100010u);

    assert(!gpu.commandTrace().empty());
    const auto& command = gpu.commandTrace().back();
    assert(command.kind == psxrecomp::runtime::GpuCommandKind::FillRectangle);

    const auto& frame = gpu.frameBuffer();
    assert(!frame.empty());
    assert(frame[0] != 0);

    gpu.writeCommand(0xE1000001u);
    gpu.writeCommand(0x6400FF00u);
    gpu.writeCommand(0x00010001u);
    gpu.writeCommand(0x00020002u);

    assert(gpu.commandTrace().size() >= 3);

    gpu.selectBackend(Gpu::Backend::SemiAccurate);
    assert(gpu.backend() == Gpu::Backend::SemiAccurate);

    gpu.reset();
    gpu.writeCommand(0xE1000000u);
    gpu.writeCommand(0x6400FF00u);
    gpu.writeCommand(0x00000000u);
    gpu.writeCommand(0x00010001u);
    const auto firstColor = gpu.frameBuffer()[0];
    gpu.writeCommand(0xE1000003u);
    gpu.writeCommand(0x6400FF00u);
    gpu.writeCommand(0x00010001u);
    gpu.writeCommand(0x00010001u);
    const auto secondColor = gpu.frameBuffer()[static_cast<size_t>(1) * 1024 + 1];
    assert(firstColor != secondColor);

    gpu.selectBackend(Gpu::Backend::Software);
    assert(gpu.frameBuffer()[0] == firstColor);
    assert(gpu.frameBuffer()[static_cast<size_t>(1) * 1024 + 1] == secondColor);

    gpu.reset();

    gpu.writeCommand(0x020000FFu);
    gpu.writeCommand(0x00000000u);
    gpu.writeCommand(0x00100010u);

    auto comparison = gpu.compareCurrentFrameWithReference();
    assert(comparison.matches());

    gpu.reset();
    gpu.writeCommand(0x020000FFu);
    gpu.writeCommand(0x0000FFFFu);
    gpu.writeCommand(0x00020002u);
    assert(gpu.frameBuffer()[0] != 0);

    const auto initialDepth = gpu.fifoDepth();
    gpu.tickGpu(2);
    assert(gpu.fifoDepth() <= initialDepth);

    gpu.restoreStatus(0x12345678u);
    assert(gpu.readStatus() == 0x12345678u);

    const auto before = gpu.readStatus();
    gpu.writeStatus(0x08000020u);
    gpu.tickDisplayLine();
    const auto after = gpu.readStatus();
    assert(before != 0);
    assert(after != 0);

    return 0;
}
