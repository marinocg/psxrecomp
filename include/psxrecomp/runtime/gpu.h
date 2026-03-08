#pragma once

#include "psxrecomp/runtime/gpu_renderer.h"
#include "psxrecomp/types.h"

#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class Gpu
{
  public:
        /// Display phase within a single frame.
        enum class DisplayPhase : u8
        {
                ActiveDisplay, ///< bit 22=0, field=current
                VBlankStart,   ///< bit 22=1, field=current  (entering VBlank)
                VBlankEnd,     ///< bit 22=1, field=!current (field flips mid-VBlank)
        };

    struct DisplayWindow
    {
        u16 x = 0;
        u16 y = 0;
        u16 width = 320;
        u16 height = 240;
        bool enabled = true;
    };

    enum class Backend
    {
        Software,
        SemiAccurate,
    };

    static constexpr size_t VramWordCount = 512u * 1024u;

    void reset();

    u32 readStatus() const;
    u32 pollStatus();
    u32 readData();
    void writeStatus(u32 value);
    void restoreStatus(u32 value);

    void writeCommand(u32 value);
    void writeDma(u32 value);

    size_t fifoDepth() const;
    u32 peekFifo() const;

    const std::vector<u32>& vramWords() const;
    const std::vector<u16>& frameBuffer() const;
    std::vector<u16> frameBufferSnapshot() const;
    DisplayWindow displayWindow() const;
    const std::vector<GpuCommand>& commandTrace() const;
    size_t malformedPacketCount() const;

    FrameComparison compareCurrentFrameWithReference() const;

    void selectBackend(Backend backend);
    Backend backend() const;

    void tickGpu(u32 cycles);
    void tickDisplayLine();
    bool irqPending() const;

    /// Returns true when the GPU is in the active-display phase (not VBlank).
    bool inActiveDisplay() const;

    DisplayPhase displayPhase() const;
    bool oddField() const;
    u16 displayLine() const;

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
        u16 drawModeStatus = 0;
        u16 displayWidth = 320;
        u16 displayHeight = 240;
        u8 maskStatus = 0;
        u8 displayModeStatus = 0;
        bool displayEnabled = true;
        bool interlaced = false;
        bool irqPending = false;
        bool forceMaskBit = false;
        bool checkMaskBeforeDraw = false;
        DmaDirection dmaDirection = DmaDirection::Off;
    };

    struct TransferState
    {
        enum class Mode
        {
            None,
            CpuToVram,
            VramToCpu,
        };

        Mode mode = Mode::None;
        u16 x = 0;
        u16 y = 0;
        u16 width = 0;
        u16 height = 0;
        size_t pixelIndex = 0;
        size_t remainingWords = 0;

        bool active() const
        {
            return mode != Mode::None && remainingWords > 0;
        }
    };

    size_t expectedGp0Words(u8 opcode) const;
    size_t expectedGp1Words(u8 opcode) const;
    void appendPacketWord(bool fromGp1, u32 value);
    void processPacket(const PacketState& packet);
    GpuCommand decodePacket(const PacketState& packet) const;
    static void applyRegisterEffects(const GpuCommand& command, Registers& registers);

    static std::pair<u16, u16> decodeTransferPosition(u32 packed);
    static std::pair<u16, u16> decodeTransferSize(u32 packed);
    u16 readVramPixel(u16 x, u16 y) const;
    void writeVramPixel(u16 x, u16 y, u16 value);
    void beginCpuToVramTransfer(const PacketState& packet);
    void consumeCpuToVramWord(u32 value);
    void beginVramToCpuTransfer(const PacketState& packet);
    u32 consumeVramToCpuWord();
    void executeVramToVramBlit(const PacketState& packet);
    void updateStatusBits();
    void updateRendererState();

    u32 m_status = 0;
    u32 m_readData = 0;
    u32 m_gpuCycles = 0;
    u32 m_commandReadyCooldown = 0;
    bool m_oddField = false;
    DisplayPhase m_displayPhase = DisplayPhase::ActiveDisplay;
    u16 m_displayLine = 0;

    Registers m_registers;
    PacketState m_packet;

    std::deque<u32> m_fifo;
    std::vector<u32> m_vram;
    std::vector<u16> m_blitScratch;
    TransferState m_transferState;

    std::vector<GpuCommand> m_commandTrace;
    size_t m_malformedPacketCount = 0;
    mutable std::mutex m_rendererMutex;

    Backend m_backend = Backend::Software;
    std::unique_ptr<GpuRenderer> m_renderer = std::make_unique<SoftwareGpuRenderer>();
    SoftwareGpuRenderer m_referenceRenderer;
};

} // namespace runtime
} // namespace psxrecomp
