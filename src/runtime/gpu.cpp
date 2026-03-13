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
    const u8 opcode = static_cast<u8>((value >> 24) & 0xFFu);
    if (opcode >= 0x10u && opcode <= 0x1Fu)
    {
        handleGpuInfoRead(value);
    }
    appendPacketWord(true, value);
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
    updateStatusBits();
}

void Gpu::writeDma(u32 value)
{
    if (m_transferState.mode == TransferState::Mode::CpuToVram)
    {
        consumeCpuToVramWord(value);
        updateStatusBits();
        return;
    }

    // GP1(04h) controls the GPUSTAT request bits and DMA bookkeeping, but a
    // DMA2 RAM->GPU burst still lands on GP0 data/command input.
    if (m_fifo.size() < MAX_FIFO_DEPTH)
    {
        m_fifo.push_back(value);
    }
    // DMA feeds can burst large linked lists. If FIFO saturates we still need
    // to ingest command words so packet decoding stays in sync.
    appendPacketWord(false, value);
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
        updateStatusBits();
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

void Gpu::handleGpuInfoRead(u32 value)
{
    const u32 internalIndex = value & 0x00FFFFFFu;
    u32 internalValue = 0;
    if (tryReadInternalRegister(internalIndex, internalValue))
    {
        m_readData = internalValue;
    }
}

bool Gpu::tryReadInternalRegister(u32 index, u32& value) const
{
    const u32 normalizedIndex = index & 0x0Fu;
    switch (normalizedIndex)
    {
    case 0x02:
        value = m_registers.textureWindowStatus;
        return true;
    case 0x03:
        value = m_registers.drawAreaTopLeftStatus;
        return true;
    case 0x04:
        value = m_registers.drawAreaBottomRightStatus;
        return true;
    case 0x05:
        value = m_registers.drawingOffsetStatus;
        return true;
    case 0x07:
        value = 0x00000002u;
        return true;
    case 0x08:
        value = 0x00000000u;
        return true;
    default:
        return false;
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
