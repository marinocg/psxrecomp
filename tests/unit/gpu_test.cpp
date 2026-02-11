#include "psxrecomp/runtime/gpu.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <utility>

namespace
{
using psxrecomp::runtime::Gpu;
using psxrecomp::runtime::GpuCommandKind;

constexpr psxrecomp::u16 kVramWidth = psxrecomp::runtime::SoftwareGpuRenderer::Width;

void writePacket(Gpu& gpu, std::initializer_list<psxrecomp::u32> words)
{
    for (const auto word : words)
    {
        gpu.writeCommand(word);
    }
}

void assertLastCommand(const Gpu& gpu, GpuCommandKind kind)
{
    assert(!gpu.commandTrace().empty());
    assert(gpu.commandTrace().back().kind == kind);
}

psxrecomp::u16 readVramPixel(const Gpu& gpu, psxrecomp::u16 x, psxrecomp::u16 y)
{
    const auto& vram = gpu.vramWords();
    const size_t pixel = static_cast<size_t>(y) * kVramWidth + x;
    const psxrecomp::u32 word = vram[pixel / 2];
    if ((pixel & 1u) == 0)
    {
        return static_cast<psxrecomp::u16>(word & 0xFFFFu);
    }
    return static_cast<psxrecomp::u16>((word >> 16) & 0xFFFFu);
}
} // namespace

int main()
{
    Gpu gpu;
    gpu.reset();

    writePacket(gpu, {0x020000FFu, 0x00000000u, 0x00100010u});
    assertLastCommand(gpu, GpuCommandKind::FillRectangle);
    assert(!gpu.frameBuffer().empty());
    assert(gpu.frameBuffer()[0] != 0);
    assert(gpu.readData() == 0);

    writePacket(gpu, {0xE1000001u});
    writePacket(gpu, {0x6400FF00u, 0x00010001u, 0x00020002u, 0x00010001u});
    assertLastCommand(gpu, GpuCommandKind::DrawSprite);

    gpu.selectBackend(Gpu::Backend::SemiAccurate);
    assert(gpu.backend() == Gpu::Backend::SemiAccurate);
    gpu.selectBackend(Gpu::Backend::Software);

    gpu.reset();
    writePacket(gpu, {0x020000FFu, 0x00000000u, 0x00100010u});
    assert(gpu.compareCurrentFrameWithReference().matches());

    const std::array<std::pair<psxrecomp::u32, GpuCommandKind>, 25> decodeCases = {{
        {0x00000000u, GpuCommandKind::Nop},
        {0x01000000u, GpuCommandKind::InterruptRequest},
        {0x20000000u, GpuCommandKind::DrawTriangle},
        {0x28000000u, GpuCommandKind::DrawQuad},
        {0x40000000u, GpuCommandKind::DrawLine},
        {0x58000000u, GpuCommandKind::DrawPolyline},
        {0x60000000u, GpuCommandKind::DrawSprite},
        {0x64000000u, GpuCommandKind::DrawSprite},
        {0x80000000u, GpuCommandKind::VramToVramBlit},
        {0xA0000000u, GpuCommandKind::CpuToVramSetup},
        {0xC0000000u, GpuCommandKind::VramToCpuSetup},
        {0xE1000000u, GpuCommandKind::DrawMode},
        {0xE2000000u, GpuCommandKind::TextureWindow},
        {0xE3000000u, GpuCommandKind::DrawingAreaTopLeft},
        {0xE4000000u, GpuCommandKind::DrawingAreaBottomRight},
        {0xE5000000u, GpuCommandKind::DrawingOffset},
        {0xE6000000u, GpuCommandKind::MaskBitSetting},
        {0x00000000u, GpuCommandKind::Reset},
        {0x03000000u, GpuCommandKind::DisplayEnable},
        {0x04000000u, GpuCommandKind::DmaDirection},
        {0x05000000u, GpuCommandKind::DisplayVramStart},
        {0x06000000u, GpuCommandKind::DisplayHorizontalRange},
        {0x07000000u, GpuCommandKind::DisplayVerticalRange},
        {0x08000000u, GpuCommandKind::DisplayMode},
        {0x02000000u, GpuCommandKind::AcknowledgeIrq},
    }};

    for (size_t i = 0; i < decodeCases.size(); ++i)
    {
        gpu.reset();
        if (i >= 17)
        {
            gpu.writeStatus(decodeCases[i].first);
        }
        else
        {
            const auto opcode = static_cast<psxrecomp::u8>((decodeCases[i].first >> 24) & 0xFF);
            if (opcode == 0x02)
            {
                writePacket(gpu, {0x02000000u, 0x00000000u, 0x00010001u});
            }
            else if (opcode == 0x20)
            {
                writePacket(gpu, {0x20000000u, 0x0u, 0x00010000u, 0x00000001u});
            }
            else if (opcode == 0x28)
            {
                writePacket(gpu, {0x28000000u, 0x0u, 0x00010000u, 0x00000001u, 0x00010001u});
            }
            else if (opcode == 0x40)
            {
                writePacket(gpu, {0x40000000u, 0x00000000u, 0x00010001u, 0x0u});
            }
            else if (opcode == 0x58)
            {
                writePacket(gpu, {0x58000000u});
            }
            else if (opcode == 0x60)
            {
                writePacket(gpu, {0x60000000u, 0x00000000u, 0x00010001u});
            }
            else if (opcode == 0x64)
            {
                writePacket(gpu, {0x64000000u, 0x00010001u, 0x00200010u, 0x00020002u});
            }
            else if (opcode == 0x80)
            {
                writePacket(gpu, {0x80000000u, 0x0u, 0x0u, 0x00010001u});
            }
            else if (opcode == 0xA0 || opcode == 0xC0)
            {
                writePacket(gpu, {decodeCases[i].first, 0x0u, 0x00010001u});
            }
            else
            {
                writePacket(gpu, {decodeCases[i].first});
            }
        }

        assertLastCommand(gpu, decodeCases[i].second);
    }

    gpu.reset();
    const auto traceBeforeMalformed = gpu.commandTrace().size();
    gpu.writeCommand(0x20000000u);
    gpu.writeStatus(0x03000000u);
    assert(gpu.commandTrace().size() == traceBeforeMalformed + 1);
    assert(gpu.malformedPacketCount() == 1);
    assertLastCommand(gpu, GpuCommandKind::DisplayEnable);

    gpu.reset();
    while (gpu.fifoDepth() < 64)
    {
        gpu.writeCommand(0x00000000u);
    }
    const auto statusWhenFull = gpu.readStatus();
    assert((statusWhenFull & (1u << 26)) == 0);

    const auto traceBeforeOverflowAttempt = gpu.commandTrace().size();
    writePacket(gpu, {0x020000FFu, 0x00000000u, 0x00100010u});
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
    assert((statusCpuToGp0 & (1u << 28)) != 0);

    gpu.writeDma(0xAABBCCDDu);
    assert(gpu.fifoDepth() == fifoDepthBeforeOffDma + 1);
    assert(gpu.readData() == 0);

    gpu.writeStatus(0x01000000u);
    assert(gpu.fifoDepth() == 0);

    gpu.writeStatus(0x04000003u);
    const auto statusGpuToCpu = gpu.readStatus();
    assert(((statusGpuToCpu >> 29) & 0x3u) == 0x3u);

    gpu.writeStatus(0x03000001u);
    assert((gpu.readStatus() & (1u << 23)) != 0);

    gpu.writeStatus(0x08000020u);
    const auto beforeLineTick = gpu.readStatus();
    gpu.tickDisplayLine();
    const auto afterLineTick = gpu.readStatus();
    assert((beforeLineTick ^ afterLineTick) & (1u << 31));

    gpu.writeStatus(0x00000000u);
    assert((gpu.readStatus() & (1u << 31)) == 0);

    // CPU -> VRAM setup + payload with wrap-around and little-endian pixel packing.
    gpu.reset();
    writePacket(gpu, {0xA0000000u, 0x01FF03FFu, 0x00020002u});
    gpu.writeCommand(0x33441122u);
    gpu.writeCommand(0x77885566u);
    assert(readVramPixel(gpu, 1023, 511) == 0x1122u);
    assert(readVramPixel(gpu, 0, 511) == 0x3344u);
    assert(readVramPixel(gpu, 1023, 0) == 0x5566u);
    assert(readVramPixel(gpu, 0, 0) == 0x7788u);

    // VRAM -> CPU setup + readback preserves packing order.
    writePacket(gpu, {0xC0000000u, 0x01FF03FFu, 0x00020002u});
    assert(gpu.readData() == 0x33441122u);
    assert(gpu.readData() == 0x77885566u);

    // VRAM -> VRAM blit wraps edges and handles overlap via temporary buffer.
    writePacket(gpu, {0x80000000u, 0x01FF03FFu, 0x00000000u, 0x00020002u});
    assert(readVramPixel(gpu, 0, 0) == 0x1122u);
    assert(readVramPixel(gpu, 1, 0) == 0x3344u);
    assert(readVramPixel(gpu, 0, 1) == 0x5566u);
    assert(readVramPixel(gpu, 1, 1) == 0x7788u);

    // Masking rules affect transfer writes.
    gpu.reset();
    writePacket(gpu, {0xA0000000u, 0x00000000u, 0x00010001u});
    gpu.writeCommand(0x00008001u);
    writePacket(gpu, {0xE6000002u});
    writePacket(gpu, {0xA0000000u, 0x00000000u, 0x00010001u});
    gpu.writeCommand(0x00000002u);
    assert(readVramPixel(gpu, 0, 0) == 0x8001u);

    writePacket(gpu, {0xE6000001u});
    writePacket(gpu, {0xA0000000u, 0x00000001u, 0x00010001u});
    gpu.writeCommand(0x00000003u);
    assert(readVramPixel(gpu, 1, 0) == 0x8003u);

    gpu.restoreStatus(0x12345678u);
    assert(gpu.readStatus() == 0x12345678u);

    return 0;
}
