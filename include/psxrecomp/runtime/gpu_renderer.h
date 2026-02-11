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
    DrawTriangle,
    DrawQuad,
    DrawSprite,
    FillRectangle,
    DrawMode,
    TextureWindow,
    DrawingAreaTopLeft,
    DrawingAreaBottomRight,
    DrawingOffset,
    DisplayEnable,
    DisplayMode,
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

  private:
    void fillRect(s32 x, s32 y, u16 width, u16 height, u16 color);
    void drawTriangle(const GpuCommand& command);
    void drawQuad(const GpuCommand& command);
    void drawSprite(const GpuCommand& command);

    std::vector<u16> m_frameBuffer = std::vector<u16>(static_cast<size_t>(Width) * Height, 0);
    bool m_interlaced = false;
    bool m_oddField = false;
    u16 m_texturePage = 0;
    u16 m_clut = 0;
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

  private:
    SoftwareGpuRenderer m_referenceRenderer;
};

FrameComparison compareFrames(const std::vector<u16>& lhs, const std::vector<u16>& rhs);

} // namespace runtime
} // namespace psxrecomp
