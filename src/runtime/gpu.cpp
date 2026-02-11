#include "psxrecomp/runtime/gpu.h"

#include <algorithm>
#include <cstddef>

namespace psxrecomp
{
namespace runtime
{

namespace
{
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
    m_gpuCycles = 0;
    m_oddField = false;
    m_registers = {};
    m_fifo.clear();
    m_vram.assign(VramWordCount, 0);
    m_vramWriteCursor = 0;
    m_commandTrace.clear();
    m_packet = {};
    selectBackend(m_backend);
    m_referenceRenderer.reset();
    updateRendererState();
    updateStatusBits();
}

u32 Gpu::readStatus() const
{
    return m_status;
}

u32 Gpu::readData() const
{
    if (m_fifo.empty())
    {
        return 0;
    }
    return m_fifo.front();
}

void Gpu::writeStatus(u32 value)
{
    appendPacketWord(true, value);
}

void Gpu::restoreStatus(u32 value)
{
    m_status = value;
}

void Gpu::writeCommand(u32 value)
{
    if (m_fifo.size() >= MAX_FIFO_DEPTH)
    {
        updateStatusBits();
        return;
    }

    m_fifo.push_back(value);
    writeVramWord(value);
    appendPacketWord(false, value);
    updateStatusBits();
}

void Gpu::writeDma(u32 value)
{
    writeCommand(value);
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

const std::vector<GpuCommand>& Gpu::commandTrace() const
{
    return m_commandTrace;
}

FrameComparison Gpu::compareCurrentFrameWithReference() const
{
    return compareFrames(m_renderer->frameBuffer(), m_referenceRenderer.frameBuffer());
}

void Gpu::selectBackend(Backend backend)
{
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
        m_renderer->setTexturePage(replayRegisters.texturePage);
        m_renderer->setClut(replayRegisters.clut);
        m_renderer->submit(command);
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
    if (m_registers.interlaced)
    {
        m_oddField = !m_oddField;
        updateRendererState();
    }
    updateStatusBits();
}

size_t Gpu::expectedGp0Words(u8 opcode) const
{
    if (opcode == 0x02)
    {
        return 3;
    }
    if (opcode == 0x20)
    {
        return 4;
    }
    if (opcode == 0x28)
    {
        return 5;
    }
    if (opcode == 0x64)
    {
        return 3;
    }
    if (opcode >= 0xE1 && opcode <= 0xE5)
    {
        return 1;
    }

    return 1;
}

size_t Gpu::expectedGp1Words(u8 opcode) const
{
    if (opcode == 0x00 || opcode == 0x03 || opcode == 0x08)
    {
        return 1;
    }

    return 1;
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

void Gpu::applyRegisterEffects(const GpuCommand& command, Registers& registers)
{
    if (!command.fromGp1)
    {
        switch (command.kind)
        {
        case GpuCommandKind::DrawMode:
            if (!command.words.empty())
            {
                registers.texturePage = static_cast<u16>(command.words[0] & 0x7FF);
            }
            break;
        case GpuCommandKind::DrawSprite:
            if (command.words.size() > 1)
            {
                registers.clut = static_cast<u16>((command.words[1] >> 16) & 0x7FFF);
            }
            break;
        default:
            break;
        }
        return;
    }

    if (command.kind == GpuCommandKind::DisplayEnable)
    {
        if (!command.words.empty())
        {
            registers.displayEnabled = (command.words[0] & 0x1) == 0;
        }
    }
    else if (command.kind == GpuCommandKind::DisplayMode)
    {
        if (!command.words.empty())
        {
            registers.interlaced = (command.words[0] & 0x20) != 0;
        }
    }
    else if (command.kind == GpuCommandKind::Reset)
    {
        registers = {};
    }
}

void Gpu::processPacket(const PacketState& packet)
{
    auto command = decodePacket(packet);
    applyRegisterEffects(command, m_registers);

    updateRendererState();
    m_renderer->submit(command);
    m_referenceRenderer.submit(command);
    trimCommandTrace(m_commandTrace, MAX_COMMAND_TRACE);
    m_commandTrace.push_back(command);
    updateStatusBits();
}

GpuCommand Gpu::decodePacket(const PacketState& packet) const
{
    GpuCommand command;
    command.opcode = packet.opcode;
    command.fromGp1 = packet.fromGp1;
    command.words = packet.words;

    if (packet.fromGp1)
    {
        switch (packet.opcode)
        {
        case 0x00:
            command.kind = GpuCommandKind::Reset;
            break;
        case 0x03:
            command.kind = GpuCommandKind::DisplayEnable;
            break;
        case 0x08:
            command.kind = GpuCommandKind::DisplayMode;
            break;
        default:
            command.kind = GpuCommandKind::Unknown;
            break;
        }
        return command;
    }

    switch (packet.opcode)
    {
    case 0x02:
        command.kind = GpuCommandKind::FillRectangle;
        break;
    case 0x20:
        command.kind = GpuCommandKind::DrawTriangle;
        break;
    case 0x28:
        command.kind = GpuCommandKind::DrawQuad;
        break;
    case 0x64:
        command.kind = GpuCommandKind::DrawSprite;
        break;
    case 0xE1:
        command.kind = GpuCommandKind::DrawMode;
        break;
    case 0xE2:
        command.kind = GpuCommandKind::TextureWindow;
        break;
    case 0xE3:
        command.kind = GpuCommandKind::DrawingAreaTopLeft;
        break;
    case 0xE4:
        command.kind = GpuCommandKind::DrawingAreaBottomRight;
        break;
    case 0xE5:
        command.kind = GpuCommandKind::DrawingOffset;
        break;
    default:
        command.kind = GpuCommandKind::Unknown;
        break;
    }

    return command;
}

void Gpu::writeVramWord(u32 value)
{
    if (m_vram.empty())
    {
        return;
    }
    m_vram[m_vramWriteCursor] = value;
    m_vramWriteCursor = (m_vramWriteCursor + 1) % m_vram.size();
}

void Gpu::updateStatusBits()
{
    constexpr u32 statusReadyMask = 1u << 26;
    constexpr u32 dmaRequestMask = 1u << 28;
    constexpr u32 interlaceMask = 1u << 31;
    constexpr u32 statusBase = STATUS_READY & ~(statusReadyMask | dmaRequestMask | interlaceMask);

    m_status = statusBase;

    if (m_fifo.size() < MAX_FIFO_DEPTH)
    {
        m_status |= statusReadyMask;
    }
    if (!m_fifo.empty())
    {
        m_status |= dmaRequestMask;
    }
    if (m_registers.interlaced && m_oddField)
    {
        m_status |= interlaceMask;
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
