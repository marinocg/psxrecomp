#include "psxrecomp/runtime/cdrom.h"

#include "cdrom_shared.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace psxrecomp
{
namespace runtime
{

namespace
{
bool isReadCommand(u8 command)
{
    return command == 0x06u || command == 0x1Bu;
}

bool traceCdromEnabled()
{
    static const bool enabled = []()
    {
        if (const char* env = std::getenv("PSXRECOMP_TRACE_CDROM"))
        {
            return env[0] == '1';
        }
        return false;
    }();
    return enabled;
}

void traceCdrom(const char* fmt, ...)
{
    if (!traceCdromEnabled())
    {
        return;
    }

    std::fputs("[cdrom] ", stderr);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}
} // namespace

// ---------------------------------------------------------------------------
// Sector phase trace helpers
// ---------------------------------------------------------------------------

namespace
{
const char* phaseReasonName(Cdrom::SectorPhaseReason r)
{
    switch (r)
    {
    case Cdrom::SectorPhaseReason::QueuePromote:
        return "queue_promote";
    case Cdrom::SectorPhaseReason::PublishInt3:
        return "publish_int3";
    case Cdrom::SectorPhaseReason::PublishInt1:
        return "publish_int1";
    case Cdrom::SectorPhaseReason::HclrctlAck:
        return "hclrctl_ack";
    case Cdrom::SectorPhaseReason::AcceptBfrd:
        return "accept_bfrd";
    case Cdrom::SectorPhaseReason::AcceptBiosAuto:
        return "accept_bios_auto";
    case Cdrom::SectorPhaseReason::DrqstsOn:
        return "drqsts_on";
    case Cdrom::SectorPhaseReason::CpuRddatRead:
        return "cpu_rddat_read";
    case Cdrom::SectorPhaseReason::Dma3Read:
        return "dma3_read";
    case Cdrom::SectorPhaseReason::DrainComplete:
        return "drain_complete";
    case Cdrom::SectorPhaseReason::XaAudioDeliver:
        return "xa_audio_deliver";
    case Cdrom::SectorPhaseReason::CpuDataDeliver:
        return "cpu_data_deliver";
    case Cdrom::SectorPhaseReason::FilterReject:
        return "filter_reject";
    case Cdrom::SectorPhaseReason::FormatReject:
        return "format_reject";
    case Cdrom::SectorPhaseReason::SubmodeReject:
        return "submode_reject";
    default:
        return "unknown";
    }
}

const char* phaseStateName(Cdrom::SectorPhaseReason r)
{
    switch (r)
    {
    case Cdrom::SectorPhaseReason::QueuePromote:
        return "buffered";
    case Cdrom::SectorPhaseReason::PublishInt3:
        return "published";
    case Cdrom::SectorPhaseReason::PublishInt1:
        return "published";
    case Cdrom::SectorPhaseReason::AcceptBfrd:
    case Cdrom::SectorPhaseReason::AcceptBiosAuto:
    case Cdrom::SectorPhaseReason::DrqstsOn:
    case Cdrom::SectorPhaseReason::CpuRddatRead:
    case Cdrom::SectorPhaseReason::Dma3Read:
    case Cdrom::SectorPhaseReason::DrainComplete:
        return "accepted";
    case Cdrom::SectorPhaseReason::XaAudioDeliver:
        return "xa_audio";
    case Cdrom::SectorPhaseReason::CpuDataDeliver:
        return "cpu_data";
    case Cdrom::SectorPhaseReason::FilterReject:
        return "filtered";
    case Cdrom::SectorPhaseReason::FormatReject:
        return "cpu_data";
    case Cdrom::SectorPhaseReason::SubmodeReject:
        return "cpu_data";
    default:
        return "";
    }
}
} // namespace

void Cdrom::recordPhaseTrace(u32 lba, SectorPhaseReason reason)
{
    m_phaseRing[m_phaseRingHead] = PhaseTraceEntry{lba, reason};
    m_phaseRingHead = (m_phaseRingHead + 1u) % PHASE_TRACE_CAPACITY;
    if (m_phaseRingCount < PHASE_TRACE_CAPACITY)
    {
        ++m_phaseRingCount;
    }
}

size_t Cdrom::phaseTraceCount() const
{
    return m_phaseRingCount;
}

Cdrom::PhaseTraceEntry Cdrom::phaseTraceEntry(size_t index) const
{
    // Map logical index (0 = oldest) to physical ring position.
    const size_t oldest =
        (m_phaseRingHead + PHASE_TRACE_CAPACITY - m_phaseRingCount) % PHASE_TRACE_CAPACITY;
    return m_phaseRing[(oldest + index) % PHASE_TRACE_CAPACITY];
}

std::string Cdrom::formatPhaseTraceSummary(size_t last) const
{
    std::ostringstream os;
    const size_t count = m_phaseRingCount;
    os << "=== CDROM Sector Phase Trace (last " << last << " transitions) ===\n";
    const size_t start = count > last ? count - last : 0u;
    for (size_t i = start; i < count; ++i)
    {
        const PhaseTraceEntry e = phaseTraceEntry(i);
        os << "  [" << std::setw(2) << i << "] lba=0x" << std::hex << std::setw(6)
           << std::setfill('0') << e.lba << std::dec << std::setfill(' ')
           << " reason=" << std::setw(16) << std::left << phaseReasonName(e.reason) << std::right;
        const char* state = phaseStateName(e.reason);
        if (state[0] != '\0')
        {
            os << "  (" << state << ")";
        }
        os << "\n";
    }
    return os.str();
}

std::string Cdrom::formatXaClassificationSummary() const
{
    std::ostringstream os;
    const u32 int1Count = m_formatRejectCount + m_submodeRejectCount + m_cpuDeliveryCount;
    const u32 totalSectors = m_xaDeliveryCount + m_filterRejectCount + int1Count;

    const bool xaStream = (m_execution.mode & cdrom_detail::SETMODE_XA_STREAM_ENABLE) != 0;
    const bool xaFilter = (m_execution.mode & cdrom_detail::SETMODE_XA_FILTER_ENABLE) != 0;
    const bool dblSpeed = (m_execution.mode & cdrom_detail::SETMODE_DOUBLE_SPEED) != 0;

    os << "=== CDROM XA Sector Classification Summary ===\n";
    os << "  setmode:          0x" << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<unsigned>(m_execution.mode) << std::dec << std::setfill(' ')
       << " (xa_stream=" << xaStream << " xa_filter=" << xaFilter << " double_speed=" << dblSpeed
       << ")\n";
    os << "  setfilter:        file=0x" << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<unsigned>(m_execution.xaFilterFile) << " channel=0x" << std::setw(2)
       << std::setfill('0') << static_cast<unsigned>(m_execution.xaFilterChannel) << std::dec
       << std::setfill(' ') << " (" << (xaFilter ? "enabled" : "disabled") << ")\n";
    os << "  sectors_total:    " << totalSectors << "\n";
    os << "    xa_audio_deliver:  " << std::setw(6) << m_xaDeliveryCount << "  (INT1 suppressed)\n";
    os << "    cpu_data_deliver:  " << std::setw(6) << m_cpuDeliveryCount << "  (INT1 fired)\n";
    os << "    filter_reject:     " << std::setw(6) << m_filterRejectCount << "\n";
    os << "    format_reject:     " << std::setw(6) << m_formatRejectCount
       << "  (bad subheader, INT1 fired)\n";
    os << "    submode_reject:    " << std::setw(6) << m_submodeRejectCount
       << "  (Form2/non-ADPCM, INT1 fired)\n";
    os << "  INT1_count:       " << int1Count << "\n";
    os << "  INT1_suppressed:  " << m_xaDeliveryCount << "\n";
    const u32 consumedBytes =
        m_xaDeliveryCount * static_cast<u32>(cdrom_detail::XA_FORM2_USER_BYTES);
    os << "  xa_sink:          " << (m_xaAudioSink ? "real" : "stub") << "\n";
    os << "  xa_consumed:      " << m_xaDeliveryCount << " sectors, " << consumedBytes << " bytes";
    if (m_xaDeliveryCount > 0)
    {
        os << ", last_coding=0x" << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(m_xaLastCodingInfo) << std::dec << std::setfill(' ');
    }
    os << "\n";
    return os.str();
}

u32 Cdrom::currentReadCycles() const
{
    if ((m_execution.mode & cdrom_detail::SETMODE_DOUBLE_SPEED) != 0)
    {
        return CDROM_DOUBLE_SPEED_READ_CYCLES;
    }
    return CDROM_READ_CYCLES;
}

void Cdrom::reset()
{
    m_status = 0;
    m_index = 0;
    m_commandFifo.clear();
    m_responseFifo.clear();
    m_ackResponseFifo.clear();
    m_dataFifo.clear();
    m_execution.reset(CDROM_READ_CYCLES);
    m_sectorQueue.clear();
    m_bufferedReadSectors.clear();
    m_activeSector.clear();
    m_activeSectorOffset = 0;
    m_drainingSector.clear();
    m_interruptFlags = 0;
    m_interruptEnable = 0;
    m_requestControl = 0;
    m_pendingCommand.clear();
    m_lastDmaWord = 0;
    m_lastGetlocL.fill(0);
    m_dataPadByte = 0;
    m_dataPadValid = false;
    m_discBackendInitialized = false;
    m_doorOpen = false;
    m_doorCloseCycles = 0;
    // Phase trace
    m_phaseRing = {};
    m_phaseRingHead = 0;
    m_phaseRingCount = 0;
    m_activeLba = 0;
    m_drainingLba = 0;
    m_bufferedReadLbas.clear();
    m_phaseFirstCpuReadFired = false;
    m_phaseFirstDmaFired = false;
    m_phaseDrainFired = false;
    // XA classification counters
    m_xaDeliveryCount = m_filterRejectCount = m_formatRejectCount = 0;
    m_submodeRejectCount = m_cpuDeliveryCount = m_xaLastCodingInfo = 0;
    // ADPBUSY + post-stream validator
    m_xaPlaybackBusy = false;
    m_xaPlaybackBusyRoseLba = m_xaPlaybackBusyFellLba = m_xaSectorsWhileBusy = 0;
    m_streamStartXaCount = m_streamStartCpuCount = 0;
    m_streamStarted = false;
    m_cpuPayloadCaptureActive = false;
    m_cpuRecordHead = 0;
    m_cpuRecordCount = 0;
    m_publishedCpuRecord = {};
    m_publishedCpuRecordValid = false;
    m_drainingCpuRecord = {};
    m_drainingCpuRecordValid = false;
    m_dataFifoConsumedBytes = 0;
    m_irqLifecycle = {};
    m_lastCallbackDispatchType = 0;
    m_lastCallbackDispatchGeneration = 0;
    m_int4HclrctlClearCount = 0;
    m_int4TopLevelDeassertAfterAck = false;
}

void Cdrom::setDiscBackend(Disc* disc)
{
    if (!m_discBackendInitialized)
    {
        m_disc = disc;
        // Treat a null backend as "not yet initialized" so startup can mount the
        // first disc without triggering a synthetic lid-open transition.
        m_discBackendInitialized = (disc != nullptr);
        return;
    }

    if (m_disc != disc)
    {
        beginDoorOpenTransition(disc != nullptr);
    }
    m_disc = disc;
}

void Cdrom::setXaAudioSink(std::function<void(const std::vector<int16_t>&)> sink)
{
    m_xaAudioSink = std::move(sink);
}

void Cdrom::notifyDiscSwap()
{
    beginDoorOpenTransition(m_disc != nullptr);
}

void Cdrom::primeBootState(bool discPresent)
{
    m_execution.motorOn = discPresent;
    m_execution.readActive = false;
    m_execution.seekActive = false;
    m_interruptFlags = 0;
    m_responseFifo.clear();
    m_bufferedReadSectors.clear();
    m_bufferedReadLbas.clear();
    m_activeSector.clear();
    m_drainingSector.clear();
}

void Cdrom::tick(u32 cpuCycles)
{
    advanceBufferedInt1Delay(cpuCycles);

    if (m_doorOpen && m_doorCloseCycles > 0)
    {
        if (cpuCycles >= m_doorCloseCycles)
        {
            m_doorCloseCycles = 0;
            m_doorOpen = false;
        }
        else
        {
            m_doorCloseCycles -= cpuCycles;
        }
    }

    if (canExecutePendingCommand())
    {
        executePendingCommand();
    }

    if (!m_execution.readActive && !m_execution.seekActive)
    {
        return;
    }

    uint64_t remaining = cpuCycles;
    while (remaining > 0)
    {
        if (remaining < m_execution.cyclesUntilSector)
        {
            m_execution.cyclesUntilSector -= static_cast<u32>(remaining);
            break;
        }

        remaining -= m_execution.cyclesUntilSector;
        m_execution.cyclesUntilSector = currentReadCycles();

        const ReadSectorResult result = queueReadSector();
        if (result == ReadSectorResult::BufferedSector)
        {
            m_execution.seekActive = false;
            m_execution.readActive = true;
            queueInterruptEvent(cdrom_detail::INT1, {currentStat()});
        }
        else if (result == ReadSectorResult::NoHostData)
        {
            m_execution.seekActive = false;
            m_execution.readActive = true;
        }
        else if (result == ReadSectorResult::ReadFailure)
        {
            queueErrorInterrupt(cdrom_detail::ERR_READ_FAIL);
            break;
        }
        else
        {
            m_execution.seekActive = false;
            m_execution.readActive = false;
            m_execution.bufferedInt1Pending = false;
            m_execution.cyclesUntilBufferedInt1 = 0u;
            m_execution.dataEndPending = true;
            if (m_xaPlaybackBusy)
            {
                m_xaPlaybackBusy = false;
                m_xaPlaybackBusyFellLba = m_activeLba;
            }
            maybeQueueReadEndInterrupt();
            break;
        }
    }
}

void Cdrom::maybeQueueReadEndInterrupt()
{
    if (!m_execution.dataEndPending || m_execution.readActive || m_execution.seekActive ||
        !isReadCommand(m_execution.currentCommand))
    {
        return;
    }

    if (m_execution.bufferedInt1Pending || !m_bufferedReadSectors.empty() ||
        !m_execution.pendingResponseIrqs.empty())
    {
        return;
    }

    const u8 activeIrq = static_cast<u8>(m_interruptFlags & 0x07u);
    if (activeIrq == cdrom_detail::INT4)
    {
        return;
    }

    m_execution.dataEndPending = false;
    queueInterruptEvent(cdrom_detail::INT4, {currentStat()});
}

u8 Cdrom::readReg(u8 offset)
{
    switch (offset & 0x3u)
    {
    case 0:
        return readStatus();
    case 1:
        return readResponse();
    case 2:
        return readData();
    case 3:
        if (m_index == 0 || m_index == 2)
        {
            return readInterruptEnable();
        }
        return readInterruptFlags();
    default:
        return 0;
    }
}

void Cdrom::writeReg(u8 offset, u8 value)
{
    switch (offset & 0x3u)
    {
    case 0:
        m_index = value & 0x3u;
        break;
    case 1:
        if (m_index == 0)
        {
            writeCommand(value);
        }
        // Index 1: Sound Map Data Out (ignored)
        // Index 2: Sound Map Coding Info (ignored)
        // Index 3: Right-CD to Right-SPU Volume (ignored)
        break;
    case 2:
        if (m_index == 0)
        {
            writeParam(value);
        }
        else if (m_index == 1)
        {
            writeInterruptEnable(value);
        }
        // Index 2: Left-CD to Left-SPU Volume (ignored)
        // Index 3: Right-CD to Left-SPU Volume (ignored)
        break;
    case 3:
        if (m_index == 0)
        {
            writeRequestControl(value);
        }
        else if (m_index == 1)
        {
            writeInterruptFlags(value);
        }
        // Index 2: Left-CD to Right-SPU Volume (ignored)
        // Index 3: Apply Volume Changes (ignored)
        break;
    default:
        break;
    }
}

u8 Cdrom::readStatus() const
{
    u8 status = static_cast<u8>(m_index & 0x3u);
    if (m_commandFifo.empty())
    {
        status |= cdrom_detail::STATUS_PARAM_FIFO_EMPTY;
    }
    if (m_commandFifo.size() < MAX_PARAMS)
    {
        status |= cdrom_detail::STATUS_PARAM_FIFO_WRITE_READY;
    }
    if (!m_responseFifo.empty())
    {
        status |= cdrom_detail::STATUS_RESPONSE_READY;
    }
    if ((m_requestControl & cdrom_detail::REQUEST_ENABLE_BUFFER_READ) != 0 && !m_dataFifo.empty())
    {
        status |= cdrom_detail::STATUS_DATA_READY;
    }
    if (m_xaPlaybackBusy && (m_execution.readActive || m_execution.seekActive))
    {
        status |= cdrom_detail::STATUS_ADPBUSY;
    }
    status |= static_cast<u8>(m_status & cdrom_detail::STATUS_COMMAND_BUSY);
    return status;
}

u8 Cdrom::readResponse()
{
    u8 value = 0;
    if (!m_responseFifo.popFront(value))
    {
        return 0;
    }
    traceCdrom("readResponse value=0x%02X remaining=%zu irq=0x%02X", value,
               m_responseFifo.values.size(), m_interruptFlags);
    if (m_responseFifo.empty() && (m_interruptFlags & 0x07u) == 0u)
    {
        publishNextInterruptEvent();
        if (canExecutePendingCommand())
        {
            executePendingCommand();
        }
    }
    return value;
}

u8 Cdrom::readInterruptFlags() const
{
    return static_cast<u8>((m_interruptFlags & 0x1Fu) | 0xE0u);
}

u8 Cdrom::readInterruptEnable() const
{
    return static_cast<u8>((m_interruptEnable & 0x1Fu) | 0xE0u);
}

void Cdrom::writeDma(u32 value)
{
    m_lastDmaWord = value;
}

u32 Cdrom::lastDmaWord() const
{
    return m_lastDmaWord;
}

Cdrom::DebugSnapshot Cdrom::debugSnapshot() const
{
    DebugSnapshot snapshot;
    snapshot.currentCommand = m_execution.currentCommand;
    snapshot.status = readStatus();
    snapshot.interruptFlags = m_interruptFlags;
    snapshot.interruptEnable = m_interruptEnable;
    snapshot.requestControl = m_requestControl;
    snapshot.mode = m_execution.mode;
    snapshot.commandFifoSize = m_commandFifo.size();
    snapshot.responseFifoSize = m_responseFifo.values.size();
    snapshot.dataFifoSize = m_dataFifo.size();
    snapshot.pendingIrqCount = m_execution.pendingResponseIrqs.size();
    snapshot.publishedSectorSize = m_activeSector.size();
    snapshot.drainingSectorSize = m_drainingSector.size();
    snapshot.motorOn = m_execution.motorOn;
    snapshot.readActive = m_execution.readActive;
    snapshot.seekActive = m_execution.seekActive;
    snapshot.publishedSectorValid = !m_activeSector.empty();
    snapshot.drainingSectorValid = !m_drainingSector.empty();
    snapshot.publishedLba = m_activeLba;
    snapshot.drainingLba = m_drainingLba;
    return snapshot;
}

} // namespace runtime
} // namespace psxrecomp
