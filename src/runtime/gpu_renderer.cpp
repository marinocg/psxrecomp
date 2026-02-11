#include "psxrecomp/runtime/gpu_renderer.h"

#include <algorithm>

namespace psxrecomp
{
namespace runtime
{

namespace
{
u16 toColor15(u32 color24)
{
    const u16 r = static_cast<u16>((color24 >> 3) & 0x1F);
    const u16 g = static_cast<u16>((color24 >> 11) & 0x1F);
    const u16 b = static_cast<u16>((color24 >> 19) & 0x1F);
    return static_cast<u16>((b << 10) | (g << 5) | r);
}

GpuVertex decodeVertex(u32 packed)
{
    GpuVertex vertex;
    vertex.x = static_cast<s16>(packed & 0xFFFF);
    vertex.y = static_cast<s16>((packed >> 16) & 0xFFFF);
    return vertex;
}

u16 clampExtent(s32 value, u16 maximum)
{
    if (value <= 0)
    {
        return 0;
    }
    if (value >= static_cast<s32>(maximum))
    {
        return maximum;
    }
    return static_cast<u16>(value);
}
} // namespace

void SoftwareGpuRenderer::reset()
{
    std::fill(m_frameBuffer.begin(), m_frameBuffer.end(), 0);
    m_interlaced = false;
    m_oddField = false;
    m_texturePage = 0;
    m_clut = 0;
}

void SoftwareGpuRenderer::submit(const GpuCommand& command)
{
    switch (command.kind)
    {
    case GpuCommandKind::FillRectangle:
        if (command.words.size() >= 3)
        {
            const auto pos = decodeVertex(command.words[1]);
            const auto size = decodeVertex(command.words[2]);
            fillRect(pos.x, pos.y, size.x, size.y, toColor15(command.words[0]));
        }
        break;
    case GpuCommandKind::DrawTriangle:
        drawTriangle(command);
        break;
    case GpuCommandKind::DrawQuad:
        drawQuad(command);
        break;
    case GpuCommandKind::DrawSprite:
        drawSprite(command);
        break;
    default:
        break;
    }
}

const std::vector<u16>& SoftwareGpuRenderer::frameBuffer() const
{
    return m_frameBuffer;
}

void SoftwareGpuRenderer::setInterlaced(bool interlaced)
{
    m_interlaced = interlaced;
}

void SoftwareGpuRenderer::setOddField(bool oddField)
{
    m_oddField = oddField;
}

void SoftwareGpuRenderer::setTexturePage(u16 texturePage)
{
    m_texturePage = texturePage;
}

void SoftwareGpuRenderer::setClut(u16 clut)
{
    m_clut = clut;
}

void SoftwareGpuRenderer::fillRect(s32 x, s32 y, u16 width, u16 height, u16 color)
{
    const u16 xBegin = clampExtent(x, Width);
    const u16 yBegin = clampExtent(y, Height);
    const u16 xEnd = clampExtent(x + width, Width);
    const u16 yEnd = clampExtent(y + height, Height);

    for (u16 py = yBegin; py < yEnd; ++py)
    {
        if (m_interlaced && ((py & 1u) != static_cast<u16>(m_oddField)))
        {
            continue;
        }
        for (u16 px = xBegin; px < xEnd; ++px)
        {
            m_frameBuffer[static_cast<size_t>(py) * Width + px] = color;
        }
    }
}

void SoftwareGpuRenderer::drawTriangle(const GpuCommand& command)
{
    if (command.words.size() < 4)
    {
        return;
    }

    const auto v0 = decodeVertex(command.words[1]);
    const auto v1 = decodeVertex(command.words[2]);
    const auto v2 = decodeVertex(command.words[3]);

    const s16 minX = std::min({v0.x, v1.x, v2.x});
    const s16 minY = std::min({v0.y, v1.y, v2.y});
    const s16 maxX = std::max({v0.x, v1.x, v2.x});
    const s16 maxY = std::max({v0.y, v1.y, v2.y});

    const s32 width = std::max<s32>(0, static_cast<s32>(maxX) - minX + 1);
    const s32 height = std::max<s32>(0, static_cast<s32>(maxY) - minY + 1);
    fillRect(minX, minY, static_cast<u16>(width), static_cast<u16>(height),
             toColor15(command.words[0]));
}

void SoftwareGpuRenderer::drawQuad(const GpuCommand& command)
{
    if (command.words.size() < 5)
    {
        return;
    }

    const auto v0 = decodeVertex(command.words[1]);
    const auto v1 = decodeVertex(command.words[2]);
    const auto v2 = decodeVertex(command.words[3]);
    const auto v3 = decodeVertex(command.words[4]);

    const s16 minX = std::min({v0.x, v1.x, v2.x, v3.x});
    const s16 minY = std::min({v0.y, v1.y, v2.y, v3.y});
    const s16 maxX = std::max({v0.x, v1.x, v2.x, v3.x});
    const s16 maxY = std::max({v0.y, v1.y, v2.y, v3.y});

    const s32 width = std::max<s32>(0, static_cast<s32>(maxX) - minX + 1);
    const s32 height = std::max<s32>(0, static_cast<s32>(maxY) - minY + 1);
    fillRect(minX, minY, static_cast<u16>(width), static_cast<u16>(height),
             toColor15(command.words[0]));
}

void SoftwareGpuRenderer::drawSprite(const GpuCommand& command)
{
    if (command.words.size() < 3)
    {
        return;
    }
    const auto pos = decodeVertex(command.words[1]);
    const auto size = decodeVertex(command.words[2]);

    u16 color = toColor15(command.words[0]);
    if ((m_texturePage & 0x3) != 0)
    {
        color ^= 0x0003;
    }
    if ((m_clut & 0x1F) != 0)
    {
        color ^= 0x001C;
    }

    fillRect(pos.x, pos.y, static_cast<u16>(std::max<s32>(1, size.x)),
             static_cast<u16>(std::max<s32>(1, size.y)), color);
}

void SemiAccurateGpuRenderer::reset()
{
    m_referenceRenderer.reset();
}

void SemiAccurateGpuRenderer::submit(const GpuCommand& command)
{
    m_referenceRenderer.submit(command);
}

const std::vector<u16>& SemiAccurateGpuRenderer::frameBuffer() const
{
    return m_referenceRenderer.frameBuffer();
}

void SemiAccurateGpuRenderer::setInterlaced(bool interlaced)
{
    m_referenceRenderer.setInterlaced(interlaced);
}

void SemiAccurateGpuRenderer::setOddField(bool oddField)
{
    m_referenceRenderer.setOddField(oddField);
}

void SemiAccurateGpuRenderer::setTexturePage(u16 texturePage)
{
    m_referenceRenderer.setTexturePage(texturePage);
}

void SemiAccurateGpuRenderer::setClut(u16 clut)
{
    m_referenceRenderer.setClut(clut);
}

FrameComparison compareFrames(const std::vector<u16>& lhs, const std::vector<u16>& rhs)
{
    FrameComparison result;
    result.totalPixels = std::min(lhs.size(), rhs.size());
    for (size_t i = 0; i < result.totalPixels; ++i)
    {
        if (lhs[i] != rhs[i])
        {
            ++result.differentPixels;
        }
    }
    result.differentPixels += (lhs.size() > rhs.size()) ? (lhs.size() - rhs.size()) : 0;
    result.differentPixels += (rhs.size() > lhs.size()) ? (rhs.size() - lhs.size()) : 0;
    result.totalPixels = std::max(lhs.size(), rhs.size());
    return result;
}

} // namespace runtime
} // namespace psxrecomp
