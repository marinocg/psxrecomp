#pragma once

#include "psxrecomp/runtime/gpu_renderer.h"
#include "psxrecomp/types.h"

#include <deque>
#include <memory>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class Gpu
{
  public:
    enum class Backend
    {
        Software,
        SemiAccurate,
    };

    static constexpr size_t VramWordCount = 512u * 1024u;

    void reset();

    u32 readStatus() const;
    u32 readData() const;
    void writeStatus(u32 value);
    void restoreStatus(u32 value);

    void writeCommand(u32 value);
    void writeDma(u32 value);

    size_t fifoDepth() const;
    u32 peekFifo() const;

    const std::vector<u32>& vramWords() const;
    const std::vector<u16>& frameBuffer() const;
    const std::vector<GpuCommand>& commandTrace() const;
    size_t malformedPacketCount() const;

    FrameComparison compareCurrentFrameWithReference() const;

    void selectBackend(Backend backend);
    Backend backend() const;

    void tickGpu(u32 cycles);
    void tickDisplayLine();

  private:
    static constexpr u32 STATUS_READY = 0x14802000;
    static constexpr size_t MAX_FIFO_DEPTH = 64;
    static constexpr size_t MAX_COMMAND_TRACE = 16384;

    struct PacketState
    {
        u8 opcode = 0;
        bool fromGp1 = false;
        size_t expectedWords = 0;
        std::vector<u32> words;
    };

    struct Registers
    {
        enum class DmaDirection : u8
        {
            Off = 0,
            Fifo = 1,
            CpuToGp0 = 2,
            GpuReadToCpu = 3,
        };

        u16 texturePage = 0;
        u16 clut = 0;
        u16 drawAreaTopLeft = 0;
        u16 drawAreaBottomRight = 0;
        u16 drawingOffset = 0;
        u16 displayXStart = 0;
        u16 displayYStart = 0;
        u16 displayXRangeStart = 0;
        u16 displayXRangeEnd = 0;
        u16 displayYRangeStart = 0;
        u16 displayYRangeEnd = 0;
        bool displayEnabled = true;
        bool interlaced = false;
        bool irqPending = false;
        DmaDirection dmaDirection = DmaDirection::Off;
    };

    size_t expectedGp0Words(u8 opcode) const;
    size_t expectedGp1Words(u8 opcode) const;
    void appendPacketWord(bool fromGp1, u32 value);
    void processPacket(const PacketState& packet);
    GpuCommand decodePacket(const PacketState& packet) const;
    static void applyRegisterEffects(const GpuCommand& command, Registers& registers);

    void writeVramWord(u32 value);
    void updateStatusBits();
    void updateRendererState();

    u32 m_status = 0;
    u32 m_readData = 0;
    u32 m_gpuCycles = 0;
    bool m_oddField = false;

    Registers m_registers;
    PacketState m_packet;

    std::deque<u32> m_fifo;
    std::vector<u32> m_vram;
    size_t m_vramWriteCursor = 0;

    std::vector<GpuCommand> m_commandTrace;
    size_t m_malformedPacketCount = 0;

    Backend m_backend = Backend::Software;
    std::unique_ptr<GpuRenderer> m_renderer = std::make_unique<SoftwareGpuRenderer>();
    SoftwareGpuRenderer m_referenceRenderer;
};

} // namespace runtime
} // namespace psxrecomp
