#include "psxrecomp/runtime/psx_system.h"

#include <cassert>

namespace psxrecomp
{
namespace runtime
{

void PsxSystem::syncLevelInterruptSources()
{
    // Impossibility guard: when deferred CD-ROM DMA is pending, the channel's
    // CHCR bit 24 (Start/Busy) must still be asserted.
    assert(!m_pendingCdromDmaDeferred ||
           (m_dma.channel(DmaPort::Cdrom).channelControl & (1u << 24u)) != 0u);

    // PSX-SPX edge semantics: I_STAT bits latch only on false→true
    // transitions of the device request line.  Clearing I_STAT while the
    // source stays asserted must NOT recreate the bit.
    const auto raiseOnEdge = [this](bool requested, bool& prev, InterruptLine line)
    {
        const u32 lineBit = static_cast<u32>(line);
        if (requested && !prev && (m_interrupts.readStatus() & lineBit) == 0u)
        {
            m_interrupts.raise(line);
            m_debugOverlay.incrementInterruptsRaised();
            m_cpTimeline.push(CpEventKind::IStatSet, static_cast<u8>(lineBit),
                              lineBit, 0, m_interrupts.readStatus());
        }
        if (requested != prev)
        {
            const CpEventKind edgeKind =
                requested ? CpEventKind::DeviceIrqRise : CpEventKind::DeviceIrqFall;
            m_cpTimeline.push(edgeKind, static_cast<u8>(lineBit), lineBit,
                              static_cast<u32>(prev), static_cast<u32>(requested));
        }
        prev = requested;
    };

    raiseOnEdge(m_gpu.irqPending(), m_prevGpuIrq, InterruptLine::Gpu);

    // CD-ROM edge fix: irqEdgeGeneration() increments on every rise; if it
    // advanced since the last sync then at least one pulse occurred and we
    // must treat prev as false so raiseOnEdge detects the transition.
    {
        const u32 cdromGen = m_cdrom.irqEdgeGeneration();
        if (cdromGen != m_cdromIrqEdgeGeneration)
        {
            m_cdromIrqEdgeGeneration = cdromGen;
            m_prevCdromIrq = false;
        }
    }
    raiseOnEdge(m_cdrom.hasIrqRequest(), m_prevCdromIrq, InterruptLine::Cdrom);

    raiseOnEdge(m_spu.hasIrqRequest(), m_prevSpuIrq, InterruptLine::Spu);
    raiseOnEdge(m_dma.irqRequested(), m_prevDmaIrq, InterruptLine::Dma);

    // Deferred CDROM DMA: complete when the data FIFO becomes ready.
    if (m_pendingCdromDmaDeferred && (m_cdrom.readStatus() & 0x40u) != 0u)
    {
        m_pendingCdromDmaDeferred = false;
        handleDmaTransfer(DmaPort::Cdrom);
        m_prevDmaIrq = false;
    }

    syncCop0InterruptPending();
}

void PsxSystem::syncCop0InterruptPending()
{
    m_cop0.noteInterruptControllerPending(m_interrupts.isInterruptPending());
}

} // namespace runtime
} // namespace psxrecomp
