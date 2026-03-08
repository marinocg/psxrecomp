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

void writePacket([[maybe_unused]] Gpu& gpu, std::initializer_list<psxrecomp::u32> words)
{
    for (const auto word : words)
    {
        gpu.writeCommand(word);
    }
}

void assertLastCommand([[maybe_unused]] const Gpu& gpu, [[maybe_unused]] GpuCommandKind kind)
{
    assert(!gpu.commandTrace().empty());
    assert(gpu.commandTrace().back().kind == kind);
}

[[maybe_unused]] psxrecomp::u16 readVramPixel(const Gpu& gpu, psxrecomp::u16 x, psxrecomp::u16 y)
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

psxrecomp::u16 readFramePixel([[maybe_unused]] const Gpu& gpu, psxrecomp::u16 x, psxrecomp::u16 y)
{
    const auto& frame = gpu.frameBuffer();
    return frame[static_cast<size_t>(y) * kVramWidth + x];
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

    const std::array<std::pair<psxrecomp::u32, GpuCommandKind>, 26> decodeCases = {{
        {0x00000000u, GpuCommandKind::Nop},
        {0x01000000u, GpuCommandKind::Nop},
        {0x1F000000u, GpuCommandKind::InterruptRequest},
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
        if (i >= 18)
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
                writePacket(gpu, {0x40000000u, 0x00000000u, 0x00010001u});
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
    [[maybe_unused]] const auto traceBeforeMalformed = gpu.commandTrace().size();
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
    [[maybe_unused]] const auto statusWhenFull = gpu.readStatus();
    assert((statusWhenFull & (1u << 26)) == 0);

    [[maybe_unused]] const auto traceBeforeOverflowAttempt = gpu.commandTrace().size();
    writePacket(gpu, {0x020000FFu, 0x00000000u, 0x00100010u});
    assert(gpu.fifoDepth() == 64);
    assert(gpu.commandTrace().size() == traceBeforeOverflowAttempt);

    [[maybe_unused]] const auto initialDepth = gpu.fifoDepth();
    gpu.tickGpu(2);
    assert(gpu.fifoDepth() <= initialDepth);

    gpu.reset();
    [[maybe_unused]] const auto fifoDepthBeforeOffDma = gpu.fifoDepth();
    gpu.writeDma(0x12345678u);
    assert(gpu.fifoDepth() == fifoDepthBeforeOffDma);

    gpu.writeStatus(0x04000002u);
    [[maybe_unused]] const auto statusCpuToGp0 = gpu.readStatus();
    assert(((statusCpuToGp0 >> 29) & 0x3u) == 0x2u);
    assert((statusCpuToGp0 & (1u << 28)) != 0);

    gpu.writeDma(0xAABBCCDDu);
    assert(gpu.fifoDepth() == fifoDepthBeforeOffDma + 1);
    assert(gpu.readData() == 0);

    gpu.writeStatus(0x01000000u);
    assert(gpu.fifoDepth() == 0);

    gpu.writeStatus(0x04000003u);
    [[maybe_unused]] const auto statusGpuToCpu = gpu.readStatus();
    assert(((statusGpuToCpu >> 29) & 0x3u) == 0x3u);
    assert((statusGpuToCpu & (1u << 27)) != 0);

    // GPUREAD readiness bit should drop while CPU->VRAM payload transfer is active.
    writePacket(gpu, {0xA0000000u, 0x00000000u, 0x00010002u});
    assert((gpu.readStatus() & (1u << 27)) == 0);
    gpu.writeCommand(0xAAAABBBBu);

    gpu.writeStatus(0x03000001u);
    assert((gpu.readStatus() & (1u << 23)) != 0);

    // Commercial polling expectations: readiness and config bits should
    // evolve with control writes and command queue activity.
    gpu.reset();
    [[maybe_unused]] const auto statusAfterReset = gpu.readStatus();
    assert((statusAfterReset & (1u << 26)) != 0);
    assert((statusAfterReset & (1u << 28)) != 0);
    assert((statusAfterReset & (1u << 19)) == 0);

    gpu.writeStatus(0x08000024u); // 320x480 interlaced mode
    [[maybe_unused]] const auto statusAfterDisplayModeWrite = gpu.readStatus();
    assert((statusAfterDisplayModeWrite & (1u << 19)) != 0);
    assert((statusAfterDisplayModeWrite & (1u << 26)) == 0);
    gpu.tickGpu(2);
    [[maybe_unused]] const auto statusAfterDisplayModeSettled = gpu.readStatus();
    assert((statusAfterDisplayModeSettled & (1u << 19)) != 0);
    assert((statusAfterDisplayModeSettled & (1u << 26)) != 0);

    gpu.writeStatus(0x03000001u); // display disable
    assert((gpu.readStatus() & (1u << 23)) != 0);
    gpu.tickGpu(2);
    gpu.writeStatus(0x03000000u); // display enable
    assert((gpu.readStatus() & (1u << 23)) == 0);

    gpu.writeStatus(0x04000002u); // DMA CPU->GP0
    [[maybe_unused]] const auto statusAfterDmaEnable = gpu.readStatus();
    assert(((statusAfterDmaEnable >> 29) & 0x3u) == 0x2u);
    assert((statusAfterDmaEnable & (1u << 28)) != 0);
    gpu.tickGpu(2);
    gpu.writeStatus(0x04000000u); // DMA off
    [[maybe_unused]] const auto statusAfterDmaDisable = gpu.readStatus();
    assert(((statusAfterDmaDisable >> 29) & 0x3u) == 0x0u);
    assert((statusAfterDmaDisable & (1u << 28)) != 0);

    gpu.tickGpu(2);
    [[maybe_unused]] const auto statusWithEmptyQueue = gpu.readStatus();
    assert((statusWithEmptyQueue & (1u << 26)) != 0);
    gpu.writeCommand(0x00000000u);
    [[maybe_unused]] const auto statusWithQueuedCommand = gpu.readStatus();
    assert((statusWithQueuedCommand & (1u << 26)) == 0);
    assert((statusWithQueuedCommand & (1u << 28)) != 0);
    gpu.tickGpu(2);
    [[maybe_unused]] const auto statusAfterQueueDrain = gpu.readStatus();
    assert((statusAfterQueueDrain & (1u << 26)) != 0);

    gpu.writeStatus(0x08000020u);
    [[maybe_unused]] const auto beforeLineTick = gpu.readStatus();
    // Two ticks: ActiveDisplay → VBlankStart (bit 22 set, bit 31 same)
    //            VBlankStart  → VBlankEnd    (bit 31 flips)
    gpu.tickDisplayLine();
    gpu.tickDisplayLine();
    [[maybe_unused]] const auto afterLineTick = gpu.readStatus();
    assert((beforeLineTick ^ afterLineTick) & (1u << 31));

    gpu.writeStatus(0x00000000u);
    assert((gpu.readStatus() & (1u << 31)) == 0);

    // Display window decoding should honor GP1 display start and mode.
    gpu.reset();
    auto displayWindow = gpu.displayWindow();
    assert(displayWindow.enabled);
    assert(displayWindow.x == 0);
    assert(displayWindow.y == 0);
    assert(displayWindow.width == 320);
    assert(displayWindow.height == 240);

    gpu.writeStatus(0x08000001u); // 320x240
    gpu.writeStatus(0x0503C000u); // VRAM display start x=0, y=240
    displayWindow = gpu.displayWindow();
    assert(displayWindow.x == 0);
    assert(displayWindow.y == 240);
    assert(displayWindow.width == 320);
    assert(displayWindow.height == 240);

    gpu.writeStatus(0x08000040u); // 368x240 mode
    gpu.writeStatus(0x0507FFFEu); // VRAM display start x=1022, y=511
    displayWindow = gpu.displayWindow();
    assert(displayWindow.x == 1022);
    assert(displayWindow.y == 511);
    assert(displayWindow.width == 2);
    assert(displayWindow.height == 1);

    gpu.writeStatus(0x03000001u); // Display disable
    assert(!gpu.displayWindow().enabled);

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

    // Overlapping VRAM -> VRAM blit must read source before writes clobber it.
    gpu.reset();
    writePacket(gpu, {0xA0000000u, 0x00000000u, 0x00010004u});
    gpu.writeCommand(0x22221111u);
    gpu.writeCommand(0x44443333u);
    writePacket(gpu, {0x80000000u, 0x00000000u, 0x00000001u, 0x00010003u});
    assert(readVramPixel(gpu, 1, 0) == 0x1111u);
    assert(readVramPixel(gpu, 2, 0) == 0x2222u);
    assert(readVramPixel(gpu, 3, 0) == 0x3333u);

    // Odd-pixel transfers keep final pixel in low 16 bits and zero-fill high readback bits.
    gpu.reset();
    writePacket(gpu, {0xA0000000u, 0x00050005u, 0x00010003u});
    gpu.writeCommand(0xBBBBAAAAu);
    gpu.writeCommand(0xDEADCCCCu);
    assert(readVramPixel(gpu, 5, 5) == 0xAAAAu);
    assert(readVramPixel(gpu, 6, 5) == 0xBBBBu);
    assert(readVramPixel(gpu, 7, 5) == 0xCCCCu);

    writePacket(gpu, {0xC0000000u, 0x00050005u, 0x00010003u});
    assert(gpu.readData() == 0xBBBBAAAAu);
    assert(gpu.readData() == 0x0000CCCCu);

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

    // Phase 3 rasterizer coverage: clip/offset + primitive rasterization rules.
    gpu.reset();
    writePacket(gpu, {0xE3000000u}); // top-left = 0,0
    writePacket(gpu, {0xE4000808u}); // bottom-right = 8,2
    writePacket(gpu, {0xE5001001u}); // x offset +1, y offset +2
    writePacket(gpu, {0x400000FFu, 0x00000000u, 0x00000004u, 0x00000000u}); // horizontal line
    assert(readFramePixel(gpu, 1, 2) != 0);
    assert(readFramePixel(gpu, 5, 2) != 0);
    assert(readFramePixel(gpu, 10, 2) == 0);

    // Degenerate line should still draw a single pixel.
    writePacket(gpu, {0x400000FFu, 0x00000002u, 0x00000002u, 0x00000000u});
    assert(readFramePixel(gpu, 3, 2) != 0);

    // Triangle top-left fill convention should include origin and exclude outside edge.
    gpu.reset();
    writePacket(gpu, {0x200000FFu, 0x00000000u, 0x00040000u, 0x00000004u});
    assert(readFramePixel(gpu, 0, 0) != 0);
    assert(readFramePixel(gpu, 4, 4) == 0);

    // Quad decomposition fills interior from both triangle halves.
    gpu.reset();
    writePacket(gpu, {0x28000080u, 0x00020002u, 0x00060002u, 0x00020006u, 0x00060006u});
    assert(readFramePixel(gpu, 4, 4) != 0);

    // Texturing pipeline: 4/8/16-bit page selection + CLUT path.
    gpu.reset();
    // 4-bit indexed fetch: default texel index is 0, so CLUT entry 0 should be returned.
    writePacket(gpu, {0x0200FF00u, 0x00010000u, 0x00010001u});
    [[maybe_unused]] const auto clut4Color = readFramePixel(gpu, 0, 1);
    writePacket(gpu, {0xE1000001u});
    writePacket(gpu, {0x64FFFFFFu, 0x000A000Au, 0x00400003u, 0x00010001u});
    assert(readFramePixel(gpu, 10, 10) == clut4Color);

    // 8-bit indexed fetch should also resolve through CLUT entry 0.
    gpu.reset();
    writePacket(gpu, {0x020000FFu, 0x00020000u, 0x00010001u});
    [[maybe_unused]] const auto clut8Color = readFramePixel(gpu, 0, 2);
    writePacket(gpu, {0xE1000081u});
    writePacket(gpu, {0x64FFFFFFu, 0x00140014u, 0x00800001u, 0x00010001u});
    assert(readFramePixel(gpu, 20, 20) == clut8Color);

    // Textured raw-vs-modulated sprite behavior should follow opcode raw bit.
    gpu.reset();
    writePacket(gpu, {0x02FFFFFFu, 0x00000000u, 0x00010001u});
    writePacket(gpu, {0xE1000100u}); // 16-bit texture mode
    writePacket(gpu, {0x64404040u, 0x00200020u, 0x00000000u, 0x00010001u});
    [[maybe_unused]] const auto modulatedPixel = readFramePixel(gpu, 32, 32);
    writePacket(gpu, {0x65404040u, 0x00210020u, 0x00000000u, 0x00010001u});
    [[maybe_unused]] const auto rawPixel = readFramePixel(gpu, 33, 32);
    assert(modulatedPixel != rawPixel);

    // 16-bit texture fetch reads direct texel value without CLUT.
    gpu.reset();
    writePacket(gpu, {0x02FFFFFFu, 0x00000000u, 0x00010001u});
    [[maybe_unused]] const auto tex16Color = readFramePixel(gpu, 0, 0);
    writePacket(gpu, {0xE1000100u});
    writePacket(gpu, {0x64FFFFFFu, 0x001E001Eu, 0x00000000u, 0x00010001u});
    assert(readFramePixel(gpu, 30, 30) == tex16Color);

    // Variable-size monochrome rectangle should use word 2 for width/height.
    gpu.reset();
    writePacket(gpu, {0x6000FFFFu, 0x00010001u, 0x00400040u}); // pos=(1,1), size=(64,64)
    assert(readFramePixel(gpu, 1, 1) != 0);
    assert(readFramePixel(gpu, 64, 64) != 0);
    assert(readFramePixel(gpu, 65, 65) == 0);

    // Blending + mask bit behavior.
    gpu.reset();
    writePacket(gpu, {0x020000FFu, 0x00050005u, 0x00010001u});
    writePacket(gpu, {0xE6000003u}); // force mask + check masked
    writePacket(gpu, {0x22000020u, 0x00050005u, 0x00060005u, 0x00050006u});
    [[maybe_unused]] const auto blended = readFramePixel(gpu, 5, 5);
    assert((blended & 0x8000u) != 0);
    // Per PSX-SPX: "Rectangle filling is not affected by the GP0(E6h) mask
    // setting, acting as if GP0(E6h).0 and GP0(E6h).1 are both zero."
    // So the fill rect SHOULD overwrite the masked pixel.
    writePacket(gpu, {0x02000000u, 0x00050005u, 0x00010001u});
    [[maybe_unused]] const auto afterFill = readFramePixel(gpu, 5, 5);
    assert(afterFill != blended);
    assert((afterFill & 0x8000u) == 0);

    // Blend mode selection should influence semi-transparent output.
    gpu.reset();
    writePacket(gpu, {0x02008040u, 0x00080008u, 0x00010001u});
    writePacket(gpu, {0xE1000001u}); // blend mode 0, raw texture disabled
    writePacket(gpu, {0x22000020u, 0x00080008u, 0x00090008u, 0x00080009u});
    [[maybe_unused]] const auto blendMode0 = readFramePixel(gpu, 8, 8);

    gpu.reset();
    writePacket(gpu, {0x02008040u, 0x00080008u, 0x00010001u});
    writePacket(gpu, {0xE1000021u}); // blend mode 1, additive
    writePacket(gpu, {0x22000020u, 0x00080008u, 0x00090008u, 0x00080009u});
    [[maybe_unused]] const auto blendMode1 = readFramePixel(gpu, 8, 8);
    assert(blendMode0 != blendMode1);

    // Dithering should vary semi-transparent primitive output across neighboring pixels.
    gpu.reset();
    writePacket(gpu, {0x02008080u, 0x00320032u, 0x00020001u});
    writePacket(gpu, {0xE1000001u}); // blend mode 0
    writePacket(gpu, {0x2A808080u, 0x00320032u, 0x00320034u, 0x00330032u, 0x00330034u});
    [[maybe_unused]] const auto transparentNoDither = readFramePixel(gpu, 50, 50);
    assert(transparentNoDither != 0);

    gpu.reset();
    writePacket(gpu, {0x02008080u, 0x00320032u, 0x00020001u});
    writePacket(gpu, {0xE1000201u}); // blend mode 0 + dithering
    writePacket(gpu, {0x2A808080u, 0x00320032u, 0x00320034u, 0x00330032u, 0x00330034u});
    [[maybe_unused]] const auto transparentWithDither = readFramePixel(gpu, 50, 50);
    assert(transparentWithDither != 0);

    // Dithering should vary nearby primitive pixels when enabled.
    gpu.reset();
    writePacket(gpu, {0xE1000200u}); // dithering enabled
    writePacket(gpu, {0x40012345u, 0x00280028u, 0x002A0028u, 0x00000000u});
    [[maybe_unused]] const auto ditherLeft = readFramePixel(gpu, 40, 40);
    [[maybe_unused]] const auto ditherRight = readFramePixel(gpu, 41, 40);
    assert(ditherLeft != ditherRight);

    // --- Fill rect correctness tests (PSX-SPX GP0(02h) behavior) ---

    // Fill rect should NOT apply draw offset.
    gpu.reset();
    writePacket(gpu, {0xE5000A0Au});                           // draw offset = (10,10)
    writePacket(gpu, {0x020000FFu, 0x00000000u, 0x00010001u}); // fill (0,0) 1x1 blue
    // If draw offset were applied, pixel would be at (10,10); it should be at (0,0).
    assert(readFramePixel(gpu, 0, 0) != 0);
    assert(readFramePixel(gpu, 10, 10) == 0);

    // Fill rect should NOT be clipped by draw area bounds.
    gpu.reset();
    writePacket(gpu, {0xE3000000u}); // draw area top-left = (0,0)
    writePacket(gpu, {0xE4000401u}); // draw area bottom-right = (1,1) — only 2x2 area
    writePacket(gpu, {0x020000FFu, 0x00050005u, 0x00010001u}); // fill (5,5) 1x1
    // Pixel at (5,5) is outside draw area but fill rect should ignore draw area.
    assert(readFramePixel(gpu, 5, 5) != 0);

    // Fill rect should NOT be affected by mask bit setting.
    gpu.reset();
    // Write a pixel via fill rect, then set force-mask + check-mask via a
    // triangle, then try to overwrite with another fill rect.
    writePacket(gpu, {0x020000FFu, 0x000A000Au, 0x00010001u}); // fill (10,10) blue
    writePacket(gpu, {0xE6000003u});                           // force mask + check mask
    // Draw a triangle that covers (10,10) — this sets mask bit on the pixel.
    writePacket(gpu, {0x22000020u, 0x000A000Au, 0x000B000Au, 0x000A000Bu});
    [[maybe_unused]] const auto maskedPixel = readFramePixel(gpu, 10, 10);
    assert((maskedPixel & 0x8000u) != 0); // mask bit set by triangle
    // Fill rect should overwrite even though mask is set.
    writePacket(gpu, {0x0200FF00u, 0x000A000Au, 0x00010001u}); // fill green
    [[maybe_unused]] const auto afterFillMask = readFramePixel(gpu, 10, 10);
    assert(afterFillMask != maskedPixel);
    assert((afterFillMask & 0x8000u) == 0); // mask bit NOT forced for fill rect

    // Fill rect width/height=0 performs no drawing.
    gpu.reset();
    writePacket(gpu, {0x020000FFu, 0x00000000u, 0x00000000u}); // 0x0 fill
    assert(readFramePixel(gpu, 0, 0) == 0);
    assert(readFramePixel(gpu, 1023, 511) == 0);

    // Fill rect X coordinate is aligned down to 16 pixels and width is rounded
    // up to a 16-pixel multiple.
    gpu.reset();
    writePacket(gpu, {0x020000FFu, 0x00000007u, 0x00010001u});
    assert(readFramePixel(gpu, 0, 0) != 0);
    assert(readFramePixel(gpu, 15, 0) != 0);
    assert(readFramePixel(gpu, 16, 0) == 0);

    return 0;
}
