#include "psxrecomp/runtime/cdrom.h"

#include "cdrom_shared.h"
#include "psxrecomp/runtime/disc.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u8 XA_SUBMODE_EOR = 0x01;
constexpr u8 XA_SUBMODE_DATA = 0x08;
constexpr u8 XA_SUBMODE_EOF = 0x80;

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

u8 Cdrom::readData()
{
    if ((m_requestControl & cdrom_detail::REQUEST_ENABLE_BUFFER_READ) == 0)
    {
        return 0;
    }

    const bool wasNonEmpty = !m_dataFifo.empty();
    u8 value = 0;
    if (!m_dataFifo.popFront(value))
    {
        return m_dataPadValid ? m_dataPadByte : 0;
    }

    noteCpuPayloadAccepted(m_drainingLba);
    if (CpuSectorRecord* rec = currentCpuPayloadRecord())
    {
        if (rec->cpuFirstOffset == ~size_t{0})
        {
            rec->cpuFirstOffset = m_dataFifoConsumedBytes;
        }
        ++rec->cpuBytesRead;
    }
    ++m_dataFifoConsumedBytes;

    if (!m_phaseFirstCpuReadFired)
    {
        m_phaseFirstCpuReadFired = true;
        recordPhaseTrace(m_drainingLba, SectorPhaseReason::CpuRddatRead);
    }
    if (wasNonEmpty && m_dataFifo.empty() && !m_phaseDrainFired)
    {
        m_phaseDrainFired = true;
        recordPhaseTrace(m_drainingLba, SectorPhaseReason::DrainComplete);
    }

    return value;
}

u32 Cdrom::readDma()
{
    if (traceCdromEnabled())
    {
        static uint64_t sReadDmaCounter = 0;
        ++sReadDmaCounter;
        if ((sReadDmaCounter & 0x3Fu) == 1u)
        {
            traceCdrom("readDma count=%llu data=%zu active=%zu readActive=%d",
                       static_cast<unsigned long long>(sReadDmaCounter), m_dataFifo.size(),
                       m_drainingSector.size(), m_execution.readActive ? 1 : 0);
        }
    }

    // Gate all DMA reads on BFRD (REQUEST_ENABLE_BUFFER_READ, bit 7 of the
    // REQUEST register written at bank 0, offset 3).  This mirrors the CPU
    // RDDATA path: per PSX-SPX the host-visible DRQSTS signal in the status
    // register is only asserted when BFRD=1 and the data FIFO has bytes to
    // serve.  DMA3 obeys the same gate so no software bypass is possible.
    if ((m_requestControl & cdrom_detail::REQUEST_ENABLE_BUFFER_READ) == 0)
    {
        return 0;
    }

    // Sector advance is NOT automatic on FIFO exhaustion.  Per PSX-SPX the
    // host-visible data-ready cycle is per-sector:
    //   INT1 published → BFRD written 0→1 → DRQSTS rises → data readable
    //   FIFO drained   → DRQSTS falls  → next INT1 needed before BFRD re-arms
    // Silently loading the next buffered sector here would shortcut that
    // handshake and allow the game to read past a sector boundary without
    // ever observing the next INT1.  The pad byte is returned instead once
    // the accepted sector is fully consumed.

    const bool wasNonEmpty = !m_dataFifo.empty();
    bool firstByteFromFifo = false;
    u32 bytesFromFifo = 0;

    u32 value = 0;
    for (u32 i = 0; i < sizeof(u32); ++i)
    {
        u8 byte = 0;
        if (m_dataFifo.popFront(byte))
        {
            firstByteFromFifo = true;
            noteCpuPayloadAccepted(m_drainingLba);
            if (CpuSectorRecord* rec = currentCpuPayloadRecord())
            {
                if (rec->dmaFirstOffset == ~size_t{0})
                {
                    rec->dmaFirstOffset = m_dataFifoConsumedBytes;
                }
                ++rec->dmaBytesRead;
            }
            ++m_dataFifoConsumedBytes;
            ++bytesFromFifo;
        }
        else
        {
            byte = m_dataPadValid ? m_dataPadByte : 0;
        }
        value |= static_cast<u32>(byte) << (i * 8);
    }

    m_lastDmaWord = value;
    noteInt1DmaBytes(bytesFromFifo);

    if (firstByteFromFifo && !m_phaseFirstDmaFired)
    {
        m_phaseFirstDmaFired = true;
        recordPhaseTrace(m_drainingLba, SectorPhaseReason::Dma3Read);
    }
    if (wasNonEmpty && m_dataFifo.empty() && !m_phaseDrainFired)
    {
        m_phaseDrainFired = true;
        recordPhaseTrace(m_drainingLba, SectorPhaseReason::DrainComplete);
    }

    return value;
}

void Cdrom::enqueueDataSector(const std::vector<u8>& data)
{
    if (m_sectorQueue.size() >= MAX_QUEUED_SECTORS)
    {
        m_sectorQueue.pop_front();
    }
    m_sectorQueue.push_back(data);
}

void Cdrom::updateDataPadForDrainingSector()
{
    if (m_drainingSector.empty())
    {
        m_dataPadValid = false;
        return;
    }
    const bool wholeSectorMode = (m_execution.mode & cdrom_detail::SETMODE_SECTOR_SIZE_2340) != 0;
    size_t padIndex = m_drainingSector.size() - 1;
    if (wholeSectorMode && m_drainingSector.size() >= 0x924u)
    {
        padIndex = 0x924u - 4u;
    }
    else if (!wholeSectorMode && m_drainingSector.size() >= 0x800u)
    {
        padIndex = 0x800u - 8u;
    }
    m_dataPadByte = m_drainingSector[padIndex];
    m_dataPadValid = true;
}

void Cdrom::acceptPublishedSector(bool replaceExistingData)
{
    if ((m_requestControl & cdrom_detail::REQUEST_ENABLE_BUFFER_READ) == 0 ||
        m_activeSector.empty())
    {
        return;
    }

    if (!replaceExistingData && !m_dataFifo.empty())
    {
        return;
    }

    if (replaceExistingData && !m_dataFifo.empty())
    {
        traceCdrom("acceptPublishedSector supersede draining_lba=%u unread=%zu next_lba=%u",
                   m_drainingLba, m_dataFifo.size(), m_activeLba);
    }
    traceCdrom("acceptPublishedSector lba=%u replace=%d published=%zu fifo_before=%zu", m_activeLba,
               replaceExistingData ? 1 : 0, m_activeSector.size(), m_dataFifo.size());
    finalizeCpuPayloadRecord(false);
    m_dataFifo.clear();
    m_drainingSector = m_activeSector;
    m_drainingLba = m_activeLba;
    m_activeSectorOffset = 0;
    m_dataFifo.pushBackRange(m_drainingSector, 0, m_drainingSector.size(), DATA_FIFO_CAPACITY);
    m_activeSectorOffset = m_drainingSector.size();
    updateDataPadForDrainingSector();
    m_dataFifoConsumedBytes = 0;
    noteCpuPayloadAccepted(m_drainingLba);
    m_phaseFirstCpuReadFired = false;
    m_phaseFirstDmaFired = false;
    m_phaseDrainFired = false;
}

void Cdrom::loadNextSectorToFifo()
{
    // Enable buffer reads (matching what the real BIOS does before reading
    // sector data) and load the next buffered sector into the data FIFO.
    m_requestControl |= cdrom_detail::REQUEST_ENABLE_BUFFER_READ;
    acceptPublishedSector(true);
}

void Cdrom::enableDataRead()
{
    m_requestControl |= cdrom_detail::REQUEST_ENABLE_BUFFER_READ;
    // Arm (or re-arm) the active sector when FIFO is empty — covers both the
    // initial 0→1 edge and re-arm after INT1 advanced the sector while BFRD
    // remained set.  Non-empty FIFO (mid-drain) is a no-op like BFRD 1→1.
    if (!m_activeSector.empty() && m_dataFifo.empty())
    {
        acceptPublishedSector(true);
        m_liveInt1AcceptedByBiosAuto = m_liveInt1RecordValid;
        recordPhaseTrace(m_drainingLba, SectorPhaseReason::AcceptBiosAuto);
        recordPhaseTrace(m_drainingLba, SectorPhaseReason::DrqstsOn);
    }
}

} // namespace runtime
} // namespace psxrecomp
