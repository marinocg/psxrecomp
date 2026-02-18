#include "psxrecomp/runtime/gpu_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace psxrecomp
{
namespace runtime
{

namespace
{
struct BlendColor
{
    s16 r = 0;
    s16 g = 0;
    s16 b = 0;
};

u8 expand5To8(u16 value)
{
    return static_cast<u8>((value << 3) | (value >> 2));
}

u16 narrow8To5(u8 value)
{
    return static_cast<u16>(value >> 3);
}

BlendColor unpackColor(u16 color)
{
    return BlendColor{static_cast<s16>(expand5To8(color & 0x1F)),
                      static_cast<s16>(expand5To8((color >> 5) & 0x1F)),
                      static_cast<s16>(expand5To8((color >> 10) & 0x1F))};
}

u16 packColor(const BlendColor& color)
{
    const u16 r = narrow8To5(static_cast<u8>(std::clamp<s16>(color.r, 0, 255)));
    const u16 g = narrow8To5(static_cast<u8>(std::clamp<s16>(color.g, 0, 255)));
    const u16 b = narrow8To5(static_cast<u8>(std::clamp<s16>(color.b, 0, 255)));
    return static_cast<u16>((b << 10) | (g << 5) | r);
}

BlendColor applyDitherBias(const BlendColor& color, s16 x, s16 y)
{
    constexpr std::array<s16, 16> kDitherMatrix = {-4, +0, -3, +1, +2, -2, +3, -1,
                                                   -3, +1, -4, +0, +3, -1, +2, -2};
    const size_t index = static_cast<size_t>((y & 3) * 4 + (x & 3));
    const s16 bias = kDitherMatrix[index];
    return BlendColor{static_cast<s16>(std::clamp<s16>(color.r + bias, 0, 255)),
                      static_cast<s16>(std::clamp<s16>(color.g + bias, 0, 255)),
                      static_cast<s16>(std::clamp<s16>(color.b + bias, 0, 255))};
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

bool isTopLeftEdge(const GpuVertex& a, const GpuVertex& b)
{
    return (a.y == b.y && a.x < b.x) || (a.y > b.y);
}

u16 wrapCoord(s32 value, u16 size)
{
    const s32 mod = value % static_cast<s32>(size);
    return static_cast<u16>(mod < 0 ? mod + size : mod);
}
} // namespace

void SoftwareGpuRenderer::drawLineImpl(const GpuVertex& from, const GpuVertex& to, u16 color,
                                       bool transparent, bool allowDither)
{
    s32 x0 = from.x;
    s32 y0 = from.y;
    const s32 x1 = to.x;
    const s32 y1 = to.y;

    const s32 dx = std::abs(x1 - x0);
    const s32 dy = std::abs(y1 - y0);
    const s32 sx = x0 < x1 ? 1 : -1;
    const s32 sy = y0 < y1 ? 1 : -1;
    s32 err = dx - dy;

    while (true)
    {
        writePixel(static_cast<s16>(x0), static_cast<s16>(y0), color, transparent, allowDither);
        if (x0 == x1 && y0 == y1)
        {
            break;
        }
        const s32 twiceErr = err * 2;
        if (twiceErr > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if (twiceErr < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void SoftwareGpuRenderer::rasterTriangle(const GpuVertex& aIn, const GpuVertex& bIn,
                                         const GpuVertex& cIn, u16 color, bool transparent,
                                         bool allowDither)
{
    GpuVertex a = aIn;
    GpuVertex b = bIn;
    GpuVertex c = cIn;

    if (signArea2(a, b, c) < 0)
    {
        std::swap(b, c);
    }

    const s16 minX = std::max<s16>(std::min({a.x, b.x, c.x}), m_drawBounds.left);
    const s16 minY = std::max<s16>(std::min({a.y, b.y, c.y}), m_drawBounds.top);
    const s16 maxX = std::min<s16>(std::max({a.x, b.x, c.x}), m_drawBounds.right);
    const s16 maxY = std::min<s16>(std::max({a.y, b.y, c.y}), m_drawBounds.bottom);

    const bool e0TopLeft = isTopLeftEdge(a, b);
    const bool e1TopLeft = isTopLeftEdge(b, c);
    const bool e2TopLeft = isTopLeftEdge(c, a);

    for (s16 y = minY; y <= maxY; ++y)
    {
        for (s16 x = minX; x <= maxX; ++x)
        {
            const GpuVertex p{static_cast<s16>(x), static_cast<s16>(y)};
            const s32 e0 = signArea2(a, b, p);
            const s32 e1 = signArea2(b, c, p);
            const s32 e2 = signArea2(c, a, p);
            if ((e0 > 0 || (e0 == 0 && e0TopLeft)) && (e1 > 0 || (e1 == 0 && e1TopLeft)) &&
                (e2 > 0 || (e2 == 0 && e2TopLeft)))
            {
                writePixel(x, y, color, transparent, allowDither);
            }
        }
    }
}

GpuVertex SoftwareGpuRenderer::applyDrawOffset(const GpuVertex& v) const
{
    return GpuVertex{static_cast<s16>(v.x + m_drawOffset.x),
                     static_cast<s16>(v.y + m_drawOffset.y)};
}

bool SoftwareGpuRenderer::isInsideDrawBounds(s16 x, s16 y) const
{
    return x >= m_drawBounds.left && x <= m_drawBounds.right && y >= m_drawBounds.top &&
           y <= m_drawBounds.bottom && x >= 0 && x < static_cast<s16>(Width) && y >= 0 &&
           y < static_cast<s16>(Height);
}

u16 SoftwareGpuRenderer::sampleTexture(u8 u, u8 v, u16 texturePage, u16 clut) const
{
    const u8 windowMaskX = static_cast<u8>(m_textureWindow.maskX * 8);
    const u8 windowMaskY = static_cast<u8>(m_textureWindow.maskY * 8);
    const u8 windowOffsetX = static_cast<u8>(m_textureWindow.offsetX * 8);
    const u8 windowOffsetY = static_cast<u8>(m_textureWindow.offsetY * 8);

    const u8 adjustedU = static_cast<u8>((u & ~windowMaskX) | (windowOffsetX & windowMaskX));
    const u8 adjustedV = static_cast<u8>((v & ~windowMaskY) | (windowOffsetY & windowMaskY));

    const u8 textureDepthBits = static_cast<u8>((texturePage >> 7) & 0x3);
    const u16 pageX = static_cast<u16>((texturePage & 0xF) * 64);
    const u16 pageY = static_cast<u16>(((texturePage >> 4) & 0x1) * 256);

    const u16 texY = static_cast<u16>(pageY + adjustedV);

    if (textureDepthBits == 2)
    {
        const u16 texX = static_cast<u16>(pageX + adjustedU);
        return m_vramRaw[static_cast<size_t>(wrapCoord(texY, Height)) * Width +
                         wrapCoord(texX, Width)] &
               0x7FFF;
    }

    u16 packedWord = 0;
    u16 index = 0;
    if (textureDepthBits == 0)
    {
        const u16 packedX = static_cast<u16>(pageX + (adjustedU >> 2));
        packedWord = m_vramRaw[static_cast<size_t>(wrapCoord(texY, Height)) * Width +
                               wrapCoord(packedX, Width)];
        const u8 shift = static_cast<u8>((adjustedU & 0x3u) * 4u);
        index = static_cast<u16>((packedWord >> shift) & 0xFu);
    }
    else
    {
        const u16 packedX = static_cast<u16>(pageX + (adjustedU >> 1));
        packedWord = m_vramRaw[static_cast<size_t>(wrapCoord(texY, Height)) * Width +
                               wrapCoord(packedX, Width)];
        const u8 shift = static_cast<u8>((adjustedU & 0x1u) * 8u);
        index = static_cast<u16>((packedWord >> shift) & 0xFFu);
    }

    const u16 clutX = static_cast<u16>((clut & 0x3F) * 16);
    const u16 clutY = static_cast<u16>((clut >> 6) & 0x1FF);
    const u16 lookupX = static_cast<u16>(clutX + index);
    return m_vramRaw[static_cast<size_t>(wrapCoord(clutY, Height)) * Width +
                     wrapCoord(lookupX, Width)] &
           0x7FFF;
}

u16 SoftwareGpuRenderer::blendColors(u16 source, u16 destination) const
{
    const BlendColor src = unpackColor(source);
    const BlendColor dst = unpackColor(destination);
    BlendColor out;

    switch (m_blendMode)
    {
    case 0:
        out =
            BlendColor{static_cast<s16>((src.r + dst.r) / 2), static_cast<s16>((src.g + dst.g) / 2),
                       static_cast<s16>((src.b + dst.b) / 2)};
        break;
    case 1:
        out = BlendColor{static_cast<s16>(std::min<s16>(255, src.r + dst.r)),
                         static_cast<s16>(std::min<s16>(255, src.g + dst.g)),
                         static_cast<s16>(std::min<s16>(255, src.b + dst.b))};
        break;
    case 2:
        out = BlendColor{static_cast<s16>(std::max<s16>(0, dst.r - src.r)),
                         static_cast<s16>(std::max<s16>(0, dst.g - src.g)),
                         static_cast<s16>(std::max<s16>(0, dst.b - src.b))};
        break;
    default:
        out = BlendColor{static_cast<s16>(std::min<s16>(255, dst.r + src.r / 4)),
                         static_cast<s16>(std::min<s16>(255, dst.g + src.g / 4)),
                         static_cast<s16>(std::min<s16>(255, dst.b + src.b / 4))};
        break;
    }

    return packColor(out);
}

u16 SoftwareGpuRenderer::modulateColor(u16 texel, u16 vertexColor)
{
    const BlendColor tex = unpackColor(texel);
    const BlendColor vtx = unpackColor(vertexColor);
    const BlendColor out{static_cast<s16>((tex.r * vtx.r) / 128),
                         static_cast<s16>((tex.g * vtx.g) / 128),
                         static_cast<s16>((tex.b * vtx.b) / 128)};
    return packColor(out);
}

void SoftwareGpuRenderer::writePixel(s16 x, s16 y, u16 color, bool transparent, bool allowDither)
{
    if (m_interlaced && ((y & 1) != static_cast<s16>(m_oddField)))
    {
        return;
    }
    if (!isInsideDrawBounds(x, y))
    {
        return;
    }

    const size_t index = static_cast<size_t>(y) * Width + static_cast<size_t>(x);
    const u16 destination = m_frameBuffer[index];

    if (m_checkMaskBeforeDraw && (destination & 0x8000) != 0)
    {
        return;
    }

    u16 output = color;
    if (allowDither)
    {
        output = packColor(applyDitherBias(unpackColor(output), x, y));
    }
    if (transparent)
    {
        output = blendColors(output, destination);
    }
    if (m_forceMaskBit)
    {
        output |= 0x8000;
    }

    m_frameBuffer[index] = output;
    m_vramRaw[index] = output;
}

} // namespace runtime
} // namespace psxrecomp
