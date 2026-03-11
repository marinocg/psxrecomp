#include "psxrecomp/runtime/gpu.h"

namespace psxrecomp
{
namespace runtime
{

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
    constexpr u32 statusVerticalInterlace = 1u << 22;
    constexpr u32 statusDisplayDisable = 1u << 23;
    constexpr u32 statusIrqRequest = 1u << 24;
    constexpr u32 statusDmaDataRequest = 1u << 25;
    constexpr u32 statusReadyToReceiveCommand = 1u << 26;
    constexpr u32 statusReadyToSendToCpu = 1u << 27;
    constexpr u32 statusReadyToReceiveDmaBlock = 1u << 28;
    constexpr u32 statusDmaDirectionShift = 29;
    constexpr u32 statusInterlaceField = 1u << 31;

    constexpr u32 statusMirroredMask =
        statusDrawModeMask | statusMaskSettingMask | statusReverseFlag | statusHorizontalRes2 |
        statusHorizontalRes1Mask | statusInterlaceGate | statusVideoMode | statusDisplayDepth;
    constexpr u32 statusDynamicMask =
        statusVerticalInterlace | statusDisplayDisable | statusIrqRequest | statusDmaDataRequest |
        statusReadyToReceiveCommand | statusReadyToSendToCpu | statusReadyToReceiveDmaBlock |
        (0x3u << statusDmaDirectionShift) | statusInterlaceField;
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
    if (m_registers.interlaced)
    {
        m_status |= statusVerticalInterlace;
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

    const bool fifoHasRoom = m_fifo.size() < MAX_FIFO_DEPTH;
    const bool cpuToVramActive = m_transferState.mode == TransferState::Mode::CpuToVram;
    const bool commandBusy = m_commandReadyCooldown != 0;
    const bool receivingPacket = !m_packet.words.empty();
    const bool polygonOrLinePacket = receivingPacket && m_packet.expectedWords > 1 &&
                                     (m_packet.opcode >= 0x20u && m_packet.opcode <= 0x5Fu);
    const bool readyForCommandWord =
        fifoHasRoom && !cpuToVramActive && !receivingPacket && !commandBusy;
    if (readyForCommandWord)
    {
        m_status |= statusReadyToReceiveCommand;
    }

    const bool readyToSend =
        m_registers.dmaDirection == Registers::DmaDirection::GpuReadToCpu && !cpuToVramActive;
    if (readyToSend)
    {
        m_status |= statusReadyToSendToCpu;
    }

    const bool readyForDmaBlock =
        cpuToVramActive || (fifoHasRoom && !(receivingPacket && polygonOrLinePacket));
    if (readyForDmaBlock)
    {
        m_status |= statusReadyToReceiveDmaBlock;
    }

    const auto dmaDirectionBits = static_cast<u32>(m_registers.dmaDirection) & 0x3u;
    m_status |= (dmaDirectionBits << statusDmaDirectionShift);

    bool dmaDataRequest = false;
    switch (m_registers.dmaDirection)
    {
    case Registers::DmaDirection::Off:
        break;
    case Registers::DmaDirection::Fifo:
        dmaDataRequest = fifoHasRoom;
        break;
    case Registers::DmaDirection::CpuToGp0:
        dmaDataRequest = readyForDmaBlock;
        break;
    case Registers::DmaDirection::GpuReadToCpu:
        dmaDataRequest = readyToSend;
        break;
    }
    if (dmaDataRequest)
    {
        m_status |= statusDmaDataRequest;
    }

    // PSX-SPX: in 240-line modes bit 31 toggles per scanline during active
    // display and is forced low in VBlank; in 480-line interlaced modes it
    // reflects the current field and changes per frame.
    const bool activeDisplay = m_displayPhase == DisplayPhase::ActiveDisplay;
    const bool evenOddBit =
        activeDisplay && (m_registers.interlaced ? m_oddField : ((m_displayLine & 1u) != 0u));
    if (evenOddBit)
    {
        m_status |= statusInterlaceField;
    }
}

} // namespace runtime
} // namespace psxrecomp
