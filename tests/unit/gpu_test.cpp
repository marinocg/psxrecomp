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

    gpu.writeCommand(0x020000FFu);
    gpu.writeCommand(0x00000000u);
    gpu.writeCommand(0x00100010u);

    auto comparison = gpu.compareCurrentFrameWithReference();
    assert(comparison.matches());

    const auto initialDepth = gpu.fifoDepth();
    gpu.tickGpu(2);
    assert(gpu.fifoDepth() <= initialDepth);

    const auto before = gpu.readStatus();
    gpu.writeStatus(0x08000020u);
    gpu.tickDisplayLine();
    const auto after = gpu.readStatus();
    assert(before != 0);
    assert(after != 0);

    return 0;
}
