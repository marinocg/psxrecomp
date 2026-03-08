#include "psxrecomp/runtime/gpu.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <string>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u32 GpuCommandReadyCooldownCycles = 2;
constexpr u16 ActiveDisplayLines = 240u;
constexpr u16 DisplayFieldFlipLine = 252u;
constexpr u16 TotalDisplayLines = 263u;

void trimCommandTrace(std::vector<GpuCommand>& trace, size_t maxSize)
{
    if (trace.size() < maxSize)
    {
        return;
    }

    const size_t removeCount = std::max<size_t>(1, maxSize / 4);
    trace.erase(trace.begin(), trace.begin() + static_cast<std::ptrdiff_t>(removeCount));
}
} // namespace

void Gpu::reset()
{
    m_status = STATUS_READY;
    m_readData = 0;
    m_gpuCycles = 0;
    m_commandReadyCooldown = 0;
    m_oddField = false;
    m_displayPhase = DisplayPhase::ActiveDisplay;
    m_displayLine = 0;
    m_registers = {};
    m_fifo.clear();
    m_vram.assign(VramWordCount, 0);
    m_transferState = {};
    m_commandTrace.clear();
    m_packet = {};
    m_malformedPacketCount = 0;
    selectBackend(m_backend);
    {
        std::lock_guard<std::mutex> lock(m_rendererMutex);
        m_referenceRenderer.reset();
        updateRendererState();
    }
    updateStatusBits();
}

u32 Gpu::readStatus() const
{
    return m_status;
}

u32 Gpu::pollStatus()
{
    return m_status;
}

u32 Gpu::readData()
{
    if (m_transferState.mode == TransferState::Mode::VramToCpu &&
        m_transferState.remainingWords > 0)
    {
        m_readData = consumeVramToCpuWord();
        updateStatusBits();
    }
    return m_readData;
}

void Gpu::writeStatus(u32 value)
{
    appendPacketWord(true, value);
    m_commandReadyCooldown = std::max(m_commandReadyCooldown, GpuCommandReadyCooldownCycles);
}

void Gpu::restoreStatus(u32 value)
{
    m_status = value;
}

void Gpu::writeCommand(u32 value)
{
    if (m_transferState.mode == TransferState::Mode::CpuToVram)
    {
        consumeCpuToVramWord(value);
        m_commandReadyCooldown = std::max(m_commandReadyCooldown, GpuCommandReadyCooldownCycles);
        updateStatusBits();
        return;
    }

    if (m_fifo.size() >= MAX_FIFO_DEPTH)
    {
        updateStatusBits();
        return;
    }

    m_fifo.push_back(value);
    appendPacketWord(false, value);
    m_commandReadyCooldown = std::max(m_commandReadyCooldown, GpuCommandReadyCooldownCycles);
    updateStatusBits();
}

void Gpu::writeDma(u32 value)
{
    if (m_transferState.mode == TransferState::Mode::CpuToVram)
    {
        consumeCpuToVramWord(value);
        m_commandReadyCooldown = std::max(m_commandReadyCooldown, GpuCommandReadyCooldownCycles);
        updateStatusBits();
        return;
    }

    if (m_registers.dmaDirection == Registers::DmaDirection::CpuToGp0 ||
        m_registers.dmaDirection == Registers::DmaDirection::Fifo)
    {
        // DMA feeds can burst large linked lists. If FIFO saturates we still
        // need to ingest command words so packet decoding stays in sync.
        if (m_fifo.size() < MAX_FIFO_DEPTH)
        {
            m_fifo.push_back(value);
        }
        appendPacketWord(false, value);
        m_commandReadyCooldown = std::max(m_commandReadyCooldown, GpuCommandReadyCooldownCycles);
        updateStatusBits();
        return;
    }

    updateStatusBits();
}

size_t Gpu::fifoDepth() const
{
    return m_fifo.size();
}

u32 Gpu::peekFifo() const
{
    if (m_fifo.empty())
    {
        return 0;
    }
    return m_fifo.front();
}

const std::vector<u32>& Gpu::vramWords() const
{
    return m_vram;
}

const std::vector<u16>& Gpu::frameBuffer() const
{
    return m_renderer->frameBuffer();
}

std::vector<u16> Gpu::frameBufferSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_rendererMutex);
    return m_renderer->frameBuffer();
}

Gpu::DisplayWindow Gpu::displayWindow() const
{
    DisplayWindow window{};
    window.enabled = m_registers.displayEnabled;

    constexpr u16 vramWidth = SoftwareGpuRenderer::Width;
    constexpr u16 vramHeight = SoftwareGpuRenderer::Height;

    window.x = static_cast<u16>(std::min<u32>(m_registers.displayXStart, vramWidth - 1));
    window.y = static_cast<u16>(std::min<u32>(m_registers.displayYStart, vramHeight - 1));
    window.width = std::max<u16>(1, m_registers.displayWidth);
    window.height = std::max<u16>(1, m_registers.displayHeight);

    if (window.width > vramWidth - window.x)
    {
        window.width = static_cast<u16>(vramWidth - window.x);
    }
    if (window.height > vramHeight - window.y)
    {
        window.height = static_cast<u16>(vramHeight - window.y);
    }

    return window;
}

const std::vector<GpuCommand>& Gpu::commandTrace() const
{
    return m_commandTrace;
}

size_t Gpu::malformedPacketCount() const
{
    return m_malformedPacketCount;
}

FrameComparison Gpu::compareCurrentFrameWithReference() const
{
    std::lock_guard<std::mutex> lock(m_rendererMutex);
    return compareFrames(m_renderer->frameBuffer(), m_referenceRenderer.frameBuffer());
}

void Gpu::selectBackend(Backend backend)
{
    std::lock_guard<std::mutex> lock(m_rendererMutex);
    m_backend = backend;
    if (backend == Backend::Software)
    {
        m_renderer = std::make_unique<SoftwareGpuRenderer>();
    }
    else
    {
        m_renderer = std::make_unique<SemiAccurateGpuRenderer>();
    }
    m_renderer->reset();
    Registers replayRegisters{};
    for (const auto& command : m_commandTrace)
    {
        applyRegisterEffects(command, replayRegisters);
        m_renderer->setInterlaced(replayRegisters.interlaced);
        m_renderer->setOddField(m_oddField);
        GpuCommand replayCommand = command;
        replayCommand.texturePage = replayRegisters.texturePage;
        replayCommand.clut = replayRegisters.clut;
        if (!replayCommand.fromGp1 && replayCommand.kind == GpuCommandKind::DrawSprite &&
            replayCommand.words.size() >= 3 && (replayCommand.opcode & 0x04u) != 0)
        {
            replayCommand.clut = static_cast<u16>((replayCommand.words[2] >> 16) & 0x7FFF);
        }
        m_renderer->submit(replayCommand);
    }
    updateRendererState();
}

Gpu::Backend Gpu::backend() const
{
    return m_backend;
}

void Gpu::tickGpu(u32 cycles)
{
    m_gpuCycles += cycles;
    const u32 wordsToConsume = m_gpuCycles / 2;
    m_gpuCycles %= 2;

    if (m_commandReadyCooldown > cycles)
    {
        m_commandReadyCooldown -= cycles;
    }
    else
    {
        m_commandReadyCooldown = 0;
    }

    for (u32 i = 0; i < wordsToConsume && !m_fifo.empty(); ++i)
    {
        m_fifo.pop_front();
    }

    updateStatusBits();
}

void Gpu::tickDisplayLine()
{
    const u16 previousLine = m_displayLine;
    m_displayLine = static_cast<u16>((m_displayLine + 1u) % TotalDisplayLines);

    if (m_displayLine < ActiveDisplayLines)
    {
        m_displayPhase = DisplayPhase::ActiveDisplay;
    }
    else if (m_displayLine < DisplayFieldFlipLine)
    {
        m_displayPhase = DisplayPhase::VBlankStart;
    }
    else
    {
        m_displayPhase = DisplayPhase::VBlankEnd;
    }

    // Flip the field exactly once per frame, in the middle of VBlank.
    if (previousLine < DisplayFieldFlipLine && m_displayLine >= DisplayFieldFlipLine)
    {
        m_oddField = !m_oddField;
    }

    {
        std::lock_guard<std::mutex> lock(m_rendererMutex);
        updateRendererState();
    }
    updateStatusBits();
}

bool Gpu::irqPending() const
{
    return m_registers.irqPending;
}

bool Gpu::inActiveDisplay() const
{
    return m_displayPhase == DisplayPhase::ActiveDisplay;
}

Gpu::DisplayPhase Gpu::displayPhase() const
{
    return m_displayPhase;
}

bool Gpu::oddField() const
{
    return m_oddField;
}

u16 Gpu::displayLine() const
{
    return m_displayLine;
}

void Gpu::appendPacketWord(bool fromGp1, u32 value)
{
    const u8 opcode = static_cast<u8>((value >> 24) & 0xFF);
    if (m_packet.words.empty())
    {
        m_packet.opcode = opcode;
        m_packet.fromGp1 = fromGp1;
        m_packet.expectedWords = fromGp1 ? expectedGp1Words(opcode) : expectedGp0Words(opcode);
    }

    if (m_packet.fromGp1 != fromGp1)
    {
        ++m_malformedPacketCount;
        m_packet = {};
        m_packet.opcode = opcode;
        m_packet.fromGp1 = fromGp1;
        m_packet.expectedWords = fromGp1 ? expectedGp1Words(opcode) : expectedGp0Words(opcode);
    }

    m_packet.words.push_back(value);
    if (m_packet.words.size() == m_packet.expectedWords)
    {
        processPacket(m_packet);
        m_packet = {};
    }
}

void Gpu::processPacket(const PacketState& packet)
{
    GpuCommand command = decodePacket(packet);
    applyRegisterEffects(command, m_registers);
    command.texturePage = m_registers.texturePage;
    command.clut = m_registers.clut;
    if (!command.fromGp1 && command.kind == GpuCommandKind::DrawSprite &&
        command.words.size() >= 3 && (command.opcode & 0x04u) != 0)
    {
        command.clut = static_cast<u16>((command.words[2] >> 16) & 0x7FFF);
    }

    if (!command.fromGp1)
    {
        if (command.kind == GpuCommandKind::CpuToVramSetup)
        {
            beginCpuToVramTransfer(packet);
        }
        else if (command.kind == GpuCommandKind::VramToCpuSetup)
        {
            beginVramToCpuTransfer(packet);
        }
        else if (command.kind == GpuCommandKind::VramToVramBlit)
        {
            executeVramToVramBlit(packet);
        }
    }

    if (command.fromGp1 && (command.kind == GpuCommandKind::Reset ||
                            command.kind == GpuCommandKind::ResetCommandBuffer))
    {
        m_fifo.clear();
        m_packet = {};
        m_transferState = {};

        if (command.kind == GpuCommandKind::Reset)
        {
            m_gpuCycles = 0;
            m_commandReadyCooldown = 0;
            m_oddField = false;
            m_displayPhase = DisplayPhase::ActiveDisplay;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_rendererMutex);
        updateRendererState();
        m_renderer->submit(command);
        m_referenceRenderer.submit(command);
    }
    trimCommandTrace(m_commandTrace, MAX_COMMAND_TRACE);
    m_commandTrace.push_back(command);
    updateStatusBits();
}

void Gpu::updateStatusBits()
{
    constexpr u32 statusDrawModeMask = 0x000007FFu;
    constexpr u32 statusMaskSettingMask = 0x00001800u;
    constexpr u32 statusReverseFlag = 1u << 13;
    constexpr u32 statusHorizontalRes2 = 1u << 16;
    constexpr u32 statusHorizontalRes1Mask = 0x3u << 17;
    constexpr u32 statusInterlaceGate = 1u << 19;
    constexpr u32 statusVideoMode = 1u << 20;
    constexpr u32 statusDisplayDepth = 1u << 21;
    constexpr u32 statusReadyToReceiveCommand = 1u << 26;
    constexpr u32 statusReadyToSendToCpu = 1u << 27;
    constexpr u32 statusDmaRequest = 1u << 28;
    constexpr u32 statusDisplayDisable = 1u << 23;
    constexpr u32 statusIrqRequest = 1u << 24;
    constexpr u32 statusDmaDirectionShift = 29;
    constexpr u32 statusInterlaceField = 1u << 31;
    constexpr u32 statusDrawingEvenOdd = 1u << 22; // VBlank-in-progress flag

    constexpr u32 statusMirroredMask = statusDrawModeMask | statusMaskSettingMask |
                                       statusReverseFlag | statusHorizontalRes2 |
                                       statusHorizontalRes1Mask | statusInterlaceGate |
                                       statusVideoMode | statusDisplayDepth;
    constexpr u32 statusDynamicMask = statusReadyToReceiveCommand | statusReadyToSendToCpu |
                                      statusDmaRequest | statusDisplayDisable | statusIrqRequest |
                                      (0x3u << statusDmaDirectionShift) | statusInterlaceField |
                                      statusDrawingEvenOdd;
    const u32 statusBase = STATUS_READY & ~(statusDynamicMask | statusMirroredMask);

    m_status = statusBase;
    m_status |= static_cast<u32>(m_registers.drawModeStatus & statusDrawModeMask);
    m_status |= (static_cast<u32>(m_registers.maskStatus & 0x3u) << 11);

    const u32 displayMode = static_cast<u32>(m_registers.displayModeStatus);
    if ((displayMode & 0x40u) != 0)
    {
        m_status |= statusHorizontalRes2;
    }
    m_status |= (displayMode & 0x3u) << 17;
    if (m_registers.interlaced || m_registers.displayHeight > 240)
    {
        m_status |= statusInterlaceGate;
    }
    if ((displayMode & 0x08u) != 0)
    {
        m_status |= statusVideoMode;
    }
    if ((displayMode & 0x10u) != 0)
    {
        m_status |= statusDisplayDepth;
    }
    if ((displayMode & 0x80u) != 0)
    {
        m_status |= statusReverseFlag;
    }

    if (!m_registers.displayEnabled)
    {
        m_status |= statusDisplayDisable;
    }
    if (m_registers.irqPending)
    {
        m_status |= statusIrqRequest;
    }

    const bool dmaInputReady = (m_fifo.size() < MAX_FIFO_DEPTH) &&
                               (m_transferState.mode != TransferState::Mode::CpuToVram);
    const bool commandReady = m_fifo.empty() &&
                              (m_transferState.mode != TransferState::Mode::CpuToVram) &&
                              m_commandReadyCooldown == 0;
    if (commandReady)
    {
        m_status |= statusReadyToReceiveCommand;
    }

    const bool readyToSend = (m_registers.dmaDirection == Registers::DmaDirection::GpuReadToCpu) &&
                             (m_transferState.mode != TransferState::Mode::CpuToVram);
    if (readyToSend)
    {
        m_status |= statusReadyToSendToCpu;
    }

    const auto dmaDirectionBits = static_cast<u32>(m_registers.dmaDirection) & 0x3u;
    m_status |= (dmaDirectionBits << statusDmaDirectionShift);

    bool request = false;
    switch (m_registers.dmaDirection)
    {
    case Registers::DmaDirection::Off:
        // BIOS gpu_sync() polls GPUSTAT.bit28 even with DMA disabled.
        // Keep bit28 high when GP0 can accept commands.
        request = dmaInputReady;
        break;
    case Registers::DmaDirection::Fifo:
    case Registers::DmaDirection::CpuToGp0:
        request = dmaInputReady || m_transferState.mode == TransferState::Mode::CpuToVram;
        break;
    case Registers::DmaDirection::GpuReadToCpu:
        request = readyToSend;
        break;
    }
    if (request)
    {
        m_status |= statusDmaRequest;
    }

    // Bit 31 mirrors odd/even field state and is used by some VSync loops
    // even in progressive display modes.
    if (m_oddField)
    {
        m_status |= statusInterlaceField;
    }

    // Bit 22 indicates that the display is currently in VBlank.
    // PSn00bSDK VSync Phase 1 polls this bit to know when VBlank starts.
    if (m_displayPhase == DisplayPhase::VBlankStart || m_displayPhase == DisplayPhase::VBlankEnd)
    {
        m_status |= statusDrawingEvenOdd;
    }
}

void Gpu::updateRendererState()
{
    if (!m_renderer)
    {
        return;
    }
    m_renderer->setInterlaced(m_registers.interlaced);
    m_renderer->setOddField(m_oddField);
    m_renderer->setTexturePage(m_registers.texturePage);
    m_renderer->setClut(m_registers.clut);

    m_referenceRenderer.setInterlaced(m_registers.interlaced);
    m_referenceRenderer.setOddField(m_oddField);
    m_referenceRenderer.setTexturePage(m_registers.texturePage);
    m_referenceRenderer.setClut(m_registers.clut);
}

} // namespace runtime
} // namespace psxrecomp
