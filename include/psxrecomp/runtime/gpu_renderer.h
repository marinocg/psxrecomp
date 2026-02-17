#pragma once

#include "psxrecomp/types.h"

#include <vector>

namespace psxrecomp
{
namespace runtime
{

struct GpuVertex
{
    s16 x = 0;
    s16 y = 0;
};

enum class GpuCommandKind
{
    Unknown,
    Nop,
    InterruptRequest,
    DrawTriangle,
    DrawQuad,
    DrawLine,
    DrawPolyline,
    DrawSprite,
    FillRectangle,
    CpuToVramSetup,
    VramToCpuSetup,
    VramToVramBlit,
    DrawMode,
    TextureWindow,
    DrawingAreaTopLeft,
    DrawingAreaBottomRight,
    DrawingOffset,
    MaskBitSetting,
    DisplayEnable,
    DmaDirection,
    DisplayVramStart,
    DisplayHorizontalRange,
    DisplayVerticalRange,
    DisplayMode,
    AcknowledgeIrq,
    ResetCommandBuffer,
    Reset,
};

struct GpuCommand
{
    GpuCommandKind kind = GpuCommandKind::Unknown;
    u8 opcode = 0;
    bool fromGp1 = false;
    std::vector<u32> words;
};

struct FrameComparison
{
    size_t differentPixels = 0;
    size_t totalPixels = 0;

    bool matches() const
    {
        return differentPixels == 0;
    }
};

class GpuRenderer
{
  public:
    virtual ~GpuRenderer() = default;

    virtual void reset() = 0;
    virtual void submit(const GpuCommand& command) = 0;
    virtual const std::vector<u16>& frameBuffer() const = 0;
    virtual void setInterlaced(bool interlaced) = 0;
    virtual void setOddField(bool oddField) = 0;
    virtual void setTexturePage(u16 texturePage) = 0;
    virtual void setClut(u16 clut) = 0;

    /// Write a single 16-bit pixel into the renderer's VRAM at the given
    /// coordinate.  This is used by CpuToVram transfers so that texture data
    /// uploaded via DMA is visible to the renderer's texture sampler.
    virtual void writeVramPixel(u16 x, u16 y, u16 value) = 0;
};

class SoftwareGpuRenderer final : public GpuRenderer
{
  public:
    static constexpr u16 Width = 1024;
    static constexpr u16 Height = 512;

    void reset() override;
    void submit(const GpuCommand& command) override;
    const std::vector<u16>& frameBuffer() const override;
    void setInterlaced(bool interlaced) override;
    void setOddField(bool oddField) override;
    void setTexturePage(u16 texturePage) override;
    void setClut(u16 clut) override;
    void writeVramPixel(u16 x, u16 y, u16 value) override;

  private:
    struct DrawBounds
    {
        s16 left = 0;
        s16 top = 0;
        s16 right = static_cast<s16>(Width - 1);
        s16 bottom = static_cast<s16>(Height - 1);
    };

    struct DrawOffset
    {
        s16 x = 0;
        s16 y = 0;
    };

    struct TextureWindow
    {
        u8 maskX = 0;
        u8 maskY = 0;
        u8 offsetX = 0;
        u8 offsetY = 0;
    };

    void fillRect(s32 x, s32 y, u16 width, u16 height, u16 color, bool transparent,
                  bool allowDither);
    void drawTriangle(const GpuCommand& command);
    void drawQuad(const GpuCommand& command);
    void drawLine(const GpuCommand& command);
    void drawSprite(const GpuCommand& command);
    void drawLineImpl(const GpuVertex& from, const GpuVertex& to, u16 color, bool transparent,
                      bool allowDither);
    void rasterTriangle(const GpuVertex& a, const GpuVertex& b, const GpuVertex& c, u16 color,
                        bool transparent, bool allowDither);
    GpuVertex applyDrawOffset(const GpuVertex& v) const;
    bool isInsideDrawBounds(s16 x, s16 y) const;
    u16 sampleTexture(u8 u, u8 v, u16 texturePage, u16 clut) const;
    u16 blendColors(u16 source, u16 destination) const;
    static u16 modulateColor(u16 texel, u16 vertexColor);
    void writePixel(s16 x, s16 y, u16 color, bool transparent, bool allowDither);
    static bool hasSemiTransparency(const GpuCommand& command);
    void applyDrawMode(u32 value);
    void applyTextureWindow(u32 value);

    std::vector<u16> m_frameBuffer = std::vector<u16>(static_cast<size_t>(Width) * Height, 0);
    bool m_interlaced = false;
    bool m_oddField = false;
    u16 m_texturePage = 0;
    u16 m_clut = 0;
    DrawBounds m_drawBounds;
    DrawOffset m_drawOffset;
    TextureWindow m_textureWindow;
    bool m_forceMaskBit = false;
    bool m_checkMaskBeforeDraw = false;
    u8 m_blendMode = 0;
    bool m_ditheringEnabled = false;
};

class SemiAccurateGpuRenderer final : public GpuRenderer
{
  public:
    void reset() override;
    void submit(const GpuCommand& command) override;
    const std::vector<u16>& frameBuffer() const override;
    void setInterlaced(bool interlaced) override;
    void setOddField(bool oddField) override;
    void setTexturePage(u16 texturePage) override;
    void setClut(u16 clut) override;
    void writeVramPixel(u16 x, u16 y, u16 value) override;

  private:
    SoftwareGpuRenderer m_referenceRenderer;
};

FrameComparison compareFrames(const std::vector<u16>& lhs, const std::vector<u16>& rhs);

} // namespace runtime
} // namespace psxrecomp
