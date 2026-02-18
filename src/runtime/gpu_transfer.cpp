#include "psxrecomp/runtime/gpu.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace psxrecomp
{
namespace runtime
{

namespace
{
size_t transferWordCount(u16 width, u16 height)
{
    return (static_cast<size_t>(width) * static_cast<size_t>(height) + 1) / 2;
}
} // namespace

std::pair<u16, u16> Gpu::decodeTransferPosition(u32 packed)
{
    return {
        static_cast<u16>(packed & 0x3FF),
        static_cast<u16>((packed >> 16) & 0x1FF),
    };
}

std::pair<u16, u16> Gpu::decodeTransferSize(u32 packed)
{
    u16 width = static_cast<u16>(packed & 0x3FF);
    u16 height = static_cast<u16>((packed >> 16) & 0x1FF);
    if (width == 0)
    {
        width = SoftwareGpuRenderer::Width;
    }
    if (height == 0)
    {
        height = SoftwareGpuRenderer::Height;
    }
    return {width, height};
}

u16 Gpu::readVramPixel(u16 x, u16 y) const
{
    const size_t pixel =
        static_cast<size_t>(y % SoftwareGpuRenderer::Height) * SoftwareGpuRenderer::Width +
        (x % SoftwareGpuRenderer::Width);
    const u32 word = m_vram[pixel / 2];
    return static_cast<u16>(((pixel & 1u) == 0u) ? (word & 0xFFFFu) : ((word >> 16) & 0xFFFFu));
}

void Gpu::writeVramPixel(u16 x, u16 y, u16 value)
{
    const u16 wrappedX = static_cast<u16>(x % SoftwareGpuRenderer::Width);
    const u16 wrappedY = static_cast<u16>(y % SoftwareGpuRenderer::Height);
    const size_t pixel = static_cast<size_t>(wrappedY) * SoftwareGpuRenderer::Width + wrappedX;
    const size_t wordIndex = pixel / 2;
    u16 writtenValue = value;

    if (m_registers.checkMaskBeforeDraw && (readVramPixel(wrappedX, wrappedY) & 0x8000u) != 0)
    {
        return;
    }

    if (m_registers.forceMaskBit)
    {
        writtenValue |= 0x8000u;
    }

    u32 word = m_vram[wordIndex];
    if ((pixel & 1u) == 0u)
    {
        word = (word & 0xFFFF0000u) | static_cast<u32>(writtenValue);
    }
    else
    {
        word = (word & 0x0000FFFFu) | (static_cast<u32>(writtenValue) << 16);
    }

    m_vram[wordIndex] = word;

    // Mirror the write to both renderers so that texture data uploaded via
    // CpuToVram is visible to the texture sampler / framebuffer dump.
    m_renderer->writeVramPixel(wrappedX, wrappedY, writtenValue);
    m_referenceRenderer.writeVramPixel(wrappedX, wrappedY, writtenValue);
}

void Gpu::beginCpuToVramTransfer(const PacketState& packet)
{
    if (packet.words.size() < 3)
    {
        return;
    }

    const auto [x, y] = decodeTransferPosition(packet.words[1]);
    const auto [width, height] = decodeTransferSize(packet.words[2]);

    if (const char* env = std::getenv("PSXRECOMP_GPU_TRACE"); env && std::string(env) == "1")
    {
        std::fprintf(stderr,
                     "[GPU] CpuToVram: pos=(%u,%u) size=(%u,%u) raw=[0x%08X, 0x%08X, 0x%08X] "
                     "remainingWords=%zu\n",
                     x, y, width, height, packet.words[0], packet.words[1], packet.words[2],
                     transferWordCount(width, height));
    }

    m_transferState = {
        TransferState::Mode::CpuToVram, x, y, width, height, 0, transferWordCount(width, height),
    };
}

void Gpu::consumeCpuToVramWord(u32 value)
{
    if (m_transferState.mode != TransferState::Mode::CpuToVram ||
        m_transferState.remainingWords == 0)
    {
        return;
    }

    const size_t totalPixels = static_cast<size_t>(m_transferState.width) * m_transferState.height;
    const u16 lowPixel = static_cast<u16>(value & 0xFFFFu);
    const u16 highPixel = static_cast<u16>((value >> 16) & 0xFFFFu);

    const size_t firstIndex = m_transferState.pixelIndex;
    const size_t secondIndex = firstIndex + 1;

    const u16 firstX = static_cast<u16>(m_transferState.x + (firstIndex % m_transferState.width));
    const u16 firstY = static_cast<u16>(m_transferState.y + (firstIndex / m_transferState.width));
    writeVramPixel(firstX, firstY, lowPixel);

    if (secondIndex < totalPixels)
    {
        const u16 secondX =
            static_cast<u16>(m_transferState.x + (secondIndex % m_transferState.width));
        const u16 secondY =
            static_cast<u16>(m_transferState.y + (secondIndex / m_transferState.width));
        writeVramPixel(secondX, secondY, highPixel);
    }

    m_transferState.pixelIndex += 2;
    if (m_transferState.remainingWords > 0)
    {
        --m_transferState.remainingWords;
    }

    if (m_transferState.remainingWords == 0)
    {
        m_transferState = {};
    }
}

void Gpu::beginVramToCpuTransfer(const PacketState& packet)
{
    if (packet.words.size() < 3)
    {
        return;
    }

    const auto [x, y] = decodeTransferPosition(packet.words[1]);
    const auto [width, height] = decodeTransferSize(packet.words[2]);
    m_transferState = {
        TransferState::Mode::VramToCpu, x, y, width, height, 0, transferWordCount(width, height),
    };
}

u32 Gpu::consumeVramToCpuWord()
{
    if (m_transferState.mode != TransferState::Mode::VramToCpu ||
        m_transferState.remainingWords == 0)
    {
        return 0;
    }

    const size_t totalPixels = static_cast<size_t>(m_transferState.width) * m_transferState.height;
    const size_t firstIndex = m_transferState.pixelIndex;
    const size_t secondIndex = firstIndex + 1;

    const u16 firstX = static_cast<u16>(m_transferState.x + (firstIndex % m_transferState.width));
    const u16 firstY = static_cast<u16>(m_transferState.y + (firstIndex / m_transferState.width));
    const u16 lowPixel = readVramPixel(firstX, firstY);

    u16 highPixel = 0;
    if (secondIndex < totalPixels)
    {
        const u16 secondX =
            static_cast<u16>(m_transferState.x + (secondIndex % m_transferState.width));
        const u16 secondY =
            static_cast<u16>(m_transferState.y + (secondIndex / m_transferState.width));
        highPixel = readVramPixel(secondX, secondY);
    }

    m_transferState.pixelIndex += 2;
    if (m_transferState.remainingWords > 0)
    {
        --m_transferState.remainingWords;
    }

    if (m_transferState.remainingWords == 0)
    {
        m_transferState = {};
    }

    return static_cast<u32>(lowPixel) | (static_cast<u32>(highPixel) << 16);
}

void Gpu::executeVramToVramBlit(const PacketState& packet)
{
    if (packet.words.size() < 4)
    {
        return;
    }

    const auto [srcX, srcY] = decodeTransferPosition(packet.words[1]);
    const auto [dstX, dstY] = decodeTransferPosition(packet.words[2]);
    const auto [width, height] = decodeTransferSize(packet.words[3]);

    const size_t totalPixels = static_cast<size_t>(width) * height;
    m_blitScratch.resize(totalPixels);

    for (size_t i = 0; i < totalPixels; ++i)
    {
        const u16 x = static_cast<u16>(srcX + (i % width));
        const u16 y = static_cast<u16>(srcY + (i / width));
        m_blitScratch[i] = readVramPixel(x, y);
    }

    for (size_t i = 0; i < totalPixels; ++i)
    {
        const u16 x = static_cast<u16>(dstX + (i % width));
        const u16 y = static_cast<u16>(dstY + (i / width));
        writeVramPixel(x, y, m_blitScratch[i]);
    }
}

} // namespace runtime
} // namespace psxrecomp
