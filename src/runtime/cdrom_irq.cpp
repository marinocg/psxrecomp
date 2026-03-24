#include "psxrecomp/runtime/cdrom.h"

#include "cdrom_shared.h"

#include <cstdlib>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u8 XA_SUBMODE_DATA = 0x08u;
constexpr const char* kGateEnv = "PSXRECOMP_EXPERIMENT_GATE_EOF_BOUNDARY_INT1";
} // namespace

bool Cdrom::eofBoundaryPublishGateExperimentEnabled() const
{
    if (const char* env = std::getenv(kGateEnv))
    {
        return env[0] == '1';
    }
    return false;
}

bool Cdrom::shouldGateBufferedInt1AfterAck() const
{
    if (!eofBoundaryPublishGateExperimentEnabled() || !m_liveInt1RecordValid ||
        m_liveInt1AcceptedByBiosAuto || m_bufferedInt1Records.empty())
    {
        return false;
    }

    const Int1GenerationRecord& current = m_int1Records[m_liveInt1RecordIndex];
    const Int1GenerationRecord& next = m_bufferedInt1Records.front();
    const bool currentIsData = current.hasXaSub && (current.xaSubmode & XA_SUBMODE_DATA) != 0;
    const bool nextIsData = next.hasXaSub && (next.xaSubmode & XA_SUBMODE_DATA) != 0;
    const bool fileChannelChanged =
        current.hasXaSub && next.hasXaSub &&
        (current.xaFile != next.xaFile || current.xaChannel != next.xaChannel);
    return currentIsData && current.hasEof && nextIsData && fileChannelChanged;
}

void Cdrom::releaseEofBoundaryInt1PublishGate()
{
    if (!m_eofBoundaryInt1PublishGatePending)
    {
        return;
    }

    m_eofBoundaryInt1PublishGatePending = false;
    m_execution.bufferedInt1Pending =
        !m_bufferedReadSectors.empty() && (m_execution.readActive || m_execution.dataEndPending);
    m_execution.cyclesUntilBufferedInt1 =
        m_execution.bufferedInt1Pending ? CDROM_BUFFERED_INT1_DELAY_CYCLES : 0u;
    if (m_execution.pendingResponseIrqs.empty())
    {
        publishNextInterruptEvent(true);
    }
}

void Cdrom::pushResponse(u8 value)
{
    m_responseFifo.push(value, RESPONSE_CAPACITY);
}

void Cdrom::scheduleBufferedInt1Promotion()
{
    if (m_eofBoundaryInt1PublishGatePending)
    {
        m_execution.bufferedInt1Pending = false;
        m_execution.cyclesUntilBufferedInt1 = 0u;
        return;
    }
    m_execution.bufferedInt1Pending =
        !m_bufferedReadSectors.empty() && (m_execution.readActive || m_execution.dataEndPending);
    m_execution.cyclesUntilBufferedInt1 =
        m_execution.bufferedInt1Pending ? CDROM_BUFFERED_INT1_DELAY_CYCLES : 0u;
}

void Cdrom::advanceBufferedInt1Delay(u32 cpuCycles)
{
    if (!m_execution.bufferedInt1Pending)
    {
        return;
    }

    if (m_bufferedReadSectors.empty() || (!m_execution.readActive && !m_execution.dataEndPending))
    {
        m_execution.bufferedInt1Pending = false;
        m_execution.cyclesUntilBufferedInt1 = 0u;
        return;
    }

    if (m_execution.cyclesUntilBufferedInt1 > cpuCycles)
    {
        m_execution.cyclesUntilBufferedInt1 -= cpuCycles;
        return;
    }

    m_execution.cyclesUntilBufferedInt1 = 0u;
    if ((m_interruptFlags & 0x07u) != 0u || !m_responseFifo.empty())
    {
        return;
    }

    m_execution.bufferedInt1Pending = false;
    publishNextInterruptEvent(true);
}

void Cdrom::queueInterruptEvent(u8 type, std::initializer_list<u8> responses)
{
    const u8 irqType = static_cast<u8>(type & 0x07u);
    if (irqType == 0u)
    {
        return;
    }

    if (irqType == cdrom_detail::INT1)
    {
        const bool int1Active = (m_interruptFlags & 0x07u) == cdrom_detail::INT1;
        const bool int1Queued = std::any_of(
            m_execution.pendingResponseIrqs.begin(), m_execution.pendingResponseIrqs.end(),
            [](const IrqEvent& event) { return event.type == cdrom_detail::INT1; });
        if (int1Active || int1Queued || m_execution.bufferedInt1Pending)
        {
            return;
        }
    }

    if (m_execution.pendingResponseIrqs.size() >= MAX_QUEUED_IRQ_EVENTS)
    {
        m_execution.pendingResponseIrqs.pop_front();
    }

    IrqEvent event;
    event.type = irqType;
    event.responses.assign(responses.begin(), responses.end());
    m_execution.pendingResponseIrqs.push_back(std::move(event));
    ++m_irqLifecycle[irqType - 1u].queuedCount;
    publishNextInterruptEvent();
}

void Cdrom::publishNextInterruptEvent(bool allowBufferedInt1)
{
    if ((m_interruptFlags & 0x07u) != 0u || !m_responseFifo.empty())
    {
        return;
    }

    m_ackResponseFifo.clear();

    IrqEvent event;
    if (!m_execution.pendingResponseIrqs.empty())
    {
        event = std::move(m_execution.pendingResponseIrqs.front());
        m_execution.pendingResponseIrqs.pop_front();
    }
    else if (allowBufferedInt1 && !m_bufferedReadSectors.empty() &&
             (m_execution.readActive || m_execution.dataEndPending))
    {
        m_execution.bufferedInt1Pending = false;
        m_execution.cyclesUntilBufferedInt1 = 0u;
        event.type = cdrom_detail::INT1;
        event.responses = {currentStat()};
    }
    else
    {
        return;
    }

    m_interruptFlags = static_cast<u8>((m_interruptFlags & 0xF8u) | (event.type & 0x07u));
    ++m_irqEdgeGeneration;
    IrqLifecycleRecord& lifecycle = m_irqLifecycle[event.type - 1u];
    ++lifecycle.publishedCount;
    ++lifecycle.publishGeneration;
    m_responseFifo.clear();
    for (u8 byte : event.responses)
    {
        pushResponse(byte);
    }

    if (event.type == cdrom_detail::INT1 && !m_bufferedReadSectors.empty())
    {
        noteCpuPayloadSuperseded();
        m_activeSector = std::move(m_bufferedReadSectors.front());
        m_bufferedReadSectors.pop_front();
        m_activeSectorOffset = 0;
        if (!m_bufferedReadLbas.empty())
        {
            m_activeLba = m_bufferedReadLbas.front();
            m_bufferedReadLbas.pop_front();
        }
        if (!m_bufferedInt1Records.empty())
        {
            m_loadedInt1Record = m_bufferedInt1Records.front();
            m_bufferedInt1Records.pop_front();
            m_loadedInt1RecordValid = true;
        }
        else
        {
            m_loadedInt1Record = {};
            m_loadedInt1RecordValid = false;
        }
        notePublishedInt1Generation(lifecycle.publishGeneration);
        recordPhaseTrace(m_activeLba, SectorPhaseReason::PublishInt1);
        beginCpuPayloadRecord(m_activeLba, m_activeSector);
        if ((m_requestControl & cdrom_detail::REQUEST_ENABLE_BUFFER_READ) != 0)
        {
            acceptPublishedSector(true);
            recordPhaseTrace(m_drainingLba, SectorPhaseReason::DrqstsOn);
        }
    }
    else if (event.type == cdrom_detail::INT1)
    {
        noteCpuPayloadSuperseded();
        recordPhaseTrace(m_activeLba, SectorPhaseReason::PublishInt1);
    }
    else
    {
        recordPhaseTrace(m_execution.currentLba, SectorPhaseReason::PublishInt3);
    }
}

} // namespace runtime
} // namespace psxrecomp
