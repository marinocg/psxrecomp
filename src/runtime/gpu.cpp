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
    m_readData = 0;
    m_gpuCycles = 0;
    m_oddField = false;
    m_registers = {};
    m_fifo.clear();
    m_vram.assign(VramWordCount, 0);
    m_transferState = {};
    m_commandTrace.clear();
    m_packet = {};
    m_malformedPacketCount = 0;
    selectBackend(m_backend);
    m_referenceRenderer.reset();
    updateRendererState();
    updateStatusBits();
}

u32 Gpu::readStatus() const
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

    if (m_registers.dmaDirection == Registers::DmaDirection::CpuToGp0 ||
        m_registers.dmaDirection == Registers::DmaDirection::Fifo)
    {
        writeCommand(value);
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
    const auto command = decodePacket(packet);
    applyRegisterEffects(command, m_registers);

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
        }
    }

    updateRendererState();
    m_renderer->submit(command);
    m_referenceRenderer.submit(command);
    trimCommandTrace(m_commandTrace, MAX_COMMAND_TRACE);
    m_commandTrace.push_back(command);
    updateStatusBits();
}

void Gpu::updateStatusBits()
{
    constexpr u32 statusReadyToReceiveCommand = 1u << 26;
    constexpr u32 statusReadyToSendToCpu = 1u << 27;
    constexpr u32 statusDmaRequest = 1u << 28;
    constexpr u32 statusDisplayDisable = 1u << 23;
    constexpr u32 statusIrqRequest = 1u << 24;
    constexpr u32 statusDmaDirectionShift = 29;
    constexpr u32 statusInterlaceField = 1u << 31;

    constexpr u32 statusDynamicMask = statusReadyToReceiveCommand | statusReadyToSendToCpu |
                                      statusDmaRequest | statusDisplayDisable | statusIrqRequest |
                                      (0x3u << statusDmaDirectionShift) | statusInterlaceField;
    const u32 statusBase = STATUS_READY & ~statusDynamicMask;

    m_status = statusBase;

    if (!m_registers.displayEnabled)
    {
        m_status |= statusDisplayDisable;
    }
    if (m_registers.irqPending)
    {
        m_status |= statusIrqRequest;
    }

    const bool canAcceptCommands = (m_fifo.size() < MAX_FIFO_DEPTH) &&
                                   (m_transferState.mode != TransferState::Mode::CpuToVram);
    if (canAcceptCommands)
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
        request = false;
        break;
    case Registers::DmaDirection::Fifo:
    case Registers::DmaDirection::CpuToGp0:
        request = canAcceptCommands || m_transferState.mode == TransferState::Mode::CpuToVram;
        break;
    case Registers::DmaDirection::GpuReadToCpu:
        request = readyToSend;
        break;
    }
    if (request)
    {
        m_status |= statusDmaRequest;
    }

    if (m_registers.interlaced && m_oddField)
    {
        m_status |= statusInterlaceField;
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
