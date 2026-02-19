#include "psxrecomp/runtime/gpu_renderer.h"

#include <algorithm>

namespace psxrecomp
{
namespace runtime
{

namespace
{
GpuVertex decodeVertex(u32 packed)
{
    return GpuVertex{static_cast<s16>(packed & 0xFFFF), static_cast<s16>((packed >> 16) & 0xFFFF)};
}

u16 narrow8To5(u8 value)
{
    return static_cast<u16>(value >> 3);
}

u16 toColor15(u32 color24, bool /*dither*/)
{
    s16 r = static_cast<s16>(color24 & 0xFF);
    s16 g = static_cast<s16>((color24 >> 8) & 0xFF);
    s16 b = static_cast<s16>((color24 >> 16) & 0xFF);

    const u16 r5 = narrow8To5(static_cast<u8>(r));
    const u16 g5 = narrow8To5(static_cast<u8>(g));
    const u16 b5 = narrow8To5(static_cast<u8>(b));
    return static_cast<u16>((b5 << 10) | (g5 << 5) | r5);
}

s32 signArea2(const GpuVertex& a, const GpuVertex& b, const GpuVertex& c)
{
    const s32 ax = a.x;
    const s32 ay = a.y;
    const s32 bx = b.x;
    const s32 by = b.y;
    const s32 cx = c.x;
    const s32 cy = c.y;
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

s16 signExtend11(u16 value)
{
    const s16 signedValue = static_cast<s16>(value & 0x7FF);
    return (signedValue & 0x400) != 0 ? static_cast<s16>(signedValue | ~0x7FF) : signedValue;
}

} // namespace

void SoftwareGpuRenderer::reset()
{
    std::fill(m_frameBuffer.begin(), m_frameBuffer.end(), 0);
    std::fill(m_vramRaw.begin(), m_vramRaw.end(), 0);
    m_interlaced = false;
    m_oddField = false;
    m_texturePage = 0;
    m_clut = 0;
    // Restore draw bounds to full VRAM area (default-constructed values).
    // Using {} would zero-initialize the struct instead of using the
    // member default values (right=1023, bottom=511).
    m_drawBounds = DrawBounds{};
    m_drawOffset = {};
    m_textureWindow = {};
    m_forceMaskBit = false;
    m_checkMaskBeforeDraw = false;
    m_blendMode = 0;
    m_ditheringEnabled = false;
}

void SoftwareGpuRenderer::submit(const GpuCommand& command)
{
    if (command.words.empty())
    {
        return;
    }

    // Bind per-command snapshots so texture state cannot be affected by
    // interleaved command streams.
    m_texturePage = command.texturePage;
    m_clut = command.clut;

    switch (command.kind)
    {
    case GpuCommandKind::FillRectangle:
    {
        if (command.words.size() < 3)
        {
            return;
        }
        // GP0(02h) Quick Rectangle Fill.
        // Per PSX-SPX:
        //  - Uses raw VRAM coordinates (no draw offset, no draw area clipping).
        //  - Not affected by GP0(E6h) mask setting.
        //  - The color is converted from 24-bit RGB to 15-bit RGB with mask
        //    bit (bit 15) forced to 0.
        // NOTE: Real hardware rounds Xpos/Xsiz to 16-pixel boundaries and
        // wraps coordinates within VRAM. Those details are omitted here for
        // simplicity; add them when hardware-accurate fill rounding matters.
        const auto pos = decodeVertex(command.words[1]);
        const auto size = decodeVertex(command.words[2]);
        const u16 fw = static_cast<u16>(std::max<s16>(0, size.x));
        const u16 fh = static_cast<u16>(std::max<s16>(0, size.y));
        if (fw == 0 || fh == 0)
        {
            break;
        }
        const u16 color = toColor15(command.words[0], false);
        for (u16 row = 0; row < fh; ++row)
        {
            for (u16 col = 0; col < fw; ++col)
            {
                const s16 px = static_cast<s16>(pos.x + col);
                const s16 py = static_cast<s16>(pos.y + row);
                if (px >= 0 && px < static_cast<s16>(Width) && py >= 0 &&
                    py < static_cast<s16>(Height))
                {
                    const size_t index = static_cast<size_t>(py) * Width + static_cast<size_t>(px);
                    m_frameBuffer[index] = color;
                    m_vramRaw[index] = color;
                }
            }
        }
        break;
    }
    case GpuCommandKind::DrawTriangle:
        drawTriangle(command);
        break;
    case GpuCommandKind::DrawQuad:
        drawQuad(command);
        break;
    case GpuCommandKind::DrawLine:
        drawLine(command);
        break;
    case GpuCommandKind::DrawSprite:
        drawSprite(command);
        break;
    case GpuCommandKind::DrawMode:
        applyDrawMode(command.words[0]);
        break;
    case GpuCommandKind::TextureWindow:
        applyTextureWindow(command.words[0]);
        break;
    case GpuCommandKind::DrawingAreaTopLeft:
        m_drawBounds.left = static_cast<s16>(command.words[0] & 0x3FF);
        m_drawBounds.top = static_cast<s16>((command.words[0] >> 10) & 0x1FF);
        break;
    case GpuCommandKind::DrawingAreaBottomRight:
        m_drawBounds.right = static_cast<s16>(command.words[0] & 0x3FF);
        m_drawBounds.bottom = static_cast<s16>((command.words[0] >> 10) & 0x1FF);
        break;
    case GpuCommandKind::DrawingOffset:
        m_drawOffset.x = signExtend11(static_cast<u16>(command.words[0] & 0x7FF));
        m_drawOffset.y = signExtend11(static_cast<u16>((command.words[0] >> 11) & 0x7FF));
        break;
    case GpuCommandKind::MaskBitSetting:
        m_forceMaskBit = (command.words[0] & 0x1) != 0;
        m_checkMaskBeforeDraw = (command.words[0] & 0x2) != 0;
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

void SoftwareGpuRenderer::writeVramPixel(u16 x, u16 y, u16 value)
{
    const u16 wrappedX = static_cast<u16>(x % Width);
    const u16 wrappedY = static_cast<u16>(y % Height);
    const size_t index = static_cast<size_t>(wrappedY) * Width + wrappedX;
    m_vramRaw[index] = value;
    m_frameBuffer[index] = static_cast<u16>(value & 0x7FFFu);
}

void SoftwareGpuRenderer::fillRect(s32 x, s32 y, u16 width, u16 height, u16 color, bool transparent,
                                   bool allowDither)
{
    for (u16 row = 0; row < height; ++row)
    {
        for (u16 col = 0; col < width; ++col)
        {
            writePixel(static_cast<s16>(x + col), static_cast<s16>(y + row), color, transparent,
                       allowDither);
        }
    }
}

void SoftwareGpuRenderer::drawTriangle(const GpuCommand& command)
{
    if (command.words.size() < 4)
    {
        return;
    }

    GpuVertex v0 = applyDrawOffset(decodeVertex(command.words[1]));
    GpuVertex v1 = applyDrawOffset(decodeVertex(command.words[2]));
    GpuVertex v2 = applyDrawOffset(decodeVertex(command.words[3]));

    if (signArea2(v0, v1, v2) == 0)
    {
        drawLineImpl(v0, v1, toColor15(command.words[0], false), hasSemiTransparency(command),
                     m_ditheringEnabled);
        drawLineImpl(v1, v2, toColor15(command.words[0], false), hasSemiTransparency(command),
                     m_ditheringEnabled);
        drawLineImpl(v2, v0, toColor15(command.words[0], false), hasSemiTransparency(command),
                     m_ditheringEnabled);
        return;
    }

    rasterTriangle(v0, v1, v2, toColor15(command.words[0], false), hasSemiTransparency(command),
                   m_ditheringEnabled);
}

void SoftwareGpuRenderer::drawQuad(const GpuCommand& command)
{
    if (command.words.size() < 5)
    {
        return;
    }

    const u16 color = toColor15(command.words[0], false);
    const bool transparent = hasSemiTransparency(command);
    GpuVertex v0 = applyDrawOffset(decodeVertex(command.words[1]));
    GpuVertex v1 = applyDrawOffset(decodeVertex(command.words[2]));
    GpuVertex v2 = applyDrawOffset(decodeVertex(command.words[3]));
    GpuVertex v3 = applyDrawOffset(decodeVertex(command.words[4]));

    rasterTriangle(v0, v1, v2, color, transparent, m_ditheringEnabled);
    rasterTriangle(v1, v2, v3, color, transparent, m_ditheringEnabled);
}

void SoftwareGpuRenderer::drawLine(const GpuCommand& command)
{
    if (command.words.size() < 3)
    {
        return;
    }

    const GpuVertex v0 = applyDrawOffset(decodeVertex(command.words[1]));
    const GpuVertex v1 = applyDrawOffset(decodeVertex(command.words[2]));
    drawLineImpl(v0, v1, toColor15(command.words[0], false), hasSemiTransparency(command),
                 m_ditheringEnabled);
}

void SoftwareGpuRenderer::drawSprite(const GpuCommand& command)
{
    if (command.words.size() < 3)
    {
        return;
    }

    const bool textured = (command.opcode & 0x04) != 0;
    const bool transparent = hasSemiTransparency(command);
    const auto pos = applyDrawOffset(decodeVertex(command.words[1]));
    u16 width = 1;
    u16 height = 1;
    if (command.opcode >= 0x70 && command.opcode <= 0x77)
    {
        width = 8;
        height = 8;
    }
    else if (command.opcode >= 0x78 && command.opcode <= 0x7F)
    {
        width = 16;
        height = 16;
    }
    else if (command.opcode >= 0x60 && command.opcode <= 0x63 && command.words.size() >= 3)
    {
        // GP0(60h-63h): variable-size monochrome rectangle, size in word 2.
        const auto size = decodeVertex(command.words[2]);
        width = static_cast<u16>(std::max<s16>(1, size.x));
        height = static_cast<u16>(std::max<s16>(1, size.y));
    }
    else if (command.opcode >= 0x64 && command.opcode <= 0x67 && command.words.size() >= 4)
    {
        // GP0(64h-67h): variable-size textured sprite, size in word 3.
        const auto size = decodeVertex(command.words[3]);
        width = static_cast<u16>(std::max<s16>(1, size.x));
        height = static_cast<u16>(std::max<s16>(1, size.y));
    }

    const u8 baseU = static_cast<u8>(command.words.size() > 2 ? command.words[2] & 0xFF : 0);
    const u8 baseV = static_cast<u8>(command.words.size() > 2 ? (command.words[2] >> 8) & 0xFF : 0);
    const u16 vertexColor = toColor15(command.words[0], false);

    for (u16 y = 0; y < height; ++y)
    {
        for (u16 x = 0; x < width; ++x)
        {
            u16 color = vertexColor;
            if (textured)
            {
                const u8 texU = static_cast<u8>(baseU + x);
                const u8 texV = static_cast<u8>(baseV + y);
                color = sampleTexture(texU, texV, m_texturePage, m_clut);
                const bool rawTextured = (command.opcode & 0x1) != 0;
                if (!rawTextured)
                {
                    color = modulateColor(color, vertexColor);
                }
                if (color == 0)
                {
                    continue;
                }
            }
            writePixel(static_cast<s16>(pos.x + x), static_cast<s16>(pos.y + y), color, transparent,
                       m_ditheringEnabled);
        }
    }
}

bool SoftwareGpuRenderer::hasSemiTransparency(const GpuCommand& command)
{
    return (command.opcode & 0x2) != 0;
}

void SoftwareGpuRenderer::applyDrawMode(u32 value)
{
    m_blendMode = static_cast<u8>((value >> 5) & 0x3);
    m_ditheringEnabled = (value & (1u << 9)) != 0;
}

void SoftwareGpuRenderer::applyTextureWindow(u32 value)
{
    m_textureWindow.maskX = static_cast<u8>(value & 0x1F);
    m_textureWindow.maskY = static_cast<u8>((value >> 5) & 0x1F);
    m_textureWindow.offsetX = static_cast<u8>((value >> 10) & 0x1F);
    m_textureWindow.offsetY = static_cast<u8>((value >> 15) & 0x1F);
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

void SemiAccurateGpuRenderer::writeVramPixel(u16 x, u16 y, u16 value)
{
    m_referenceRenderer.writeVramPixel(x, y, value);
}

FrameComparison compareFrames(const std::vector<u16>& lhs, const std::vector<u16>& rhs)
{
    FrameComparison result;
    result.totalPixels = std::max(lhs.size(), rhs.size());

    const size_t overlap = std::min(lhs.size(), rhs.size());
    for (size_t i = 0; i < overlap; ++i)
    {
        if (lhs[i] != rhs[i])
        {
            ++result.differentPixels;
        }
    }
    result.differentPixels += (lhs.size() > rhs.size()) ? (lhs.size() - rhs.size()) : 0;
    result.differentPixels += (rhs.size() > lhs.size()) ? (rhs.size() - lhs.size()) : 0;

    return result;
}

} // namespace runtime
} // namespace psxrecomp
