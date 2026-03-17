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

    u8 value = 0;
    if (!m_dataFifo.popFront(value))
    {
        return m_dataPadValid ? m_dataPadByte : 0;
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
                       m_activeSector.size(), m_execution.readActive ? 1 : 0);
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

    const auto consumeDataByte = [this]() -> u8
    {
        u8 byte = 0;
        if (m_dataFifo.popFront(byte))
        {
            return byte;
        }

        return m_dataPadValid ? m_dataPadByte : 0;
    };

    u32 value = 0;
    for (u32 i = 0; i < sizeof(u32); ++i)
    {
        const u8 byte = consumeDataByte();
        value |= static_cast<u32>(byte) << (i * 8);
    }

    m_lastDmaWord = value;

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

void Cdrom::updateDataPadForActiveSector()
{
    if (m_activeSector.empty())
    {
        m_dataPadValid = false;
        return;
    }
    const bool wholeSectorMode = (m_execution.mode & cdrom_detail::SETMODE_SECTOR_SIZE_2340) != 0;
    size_t padIndex = m_activeSector.size() - 1;
    if (wholeSectorMode && m_activeSector.size() >= 0x924u)
    {
        padIndex = 0x924u - 4u;
    }
    else if (!wholeSectorMode && m_activeSector.size() >= 0x800u)
    {
        padIndex = 0x800u - 8u;
    }
    m_dataPadByte = m_activeSector[padIndex];
    m_dataPadValid = true;
}

void Cdrom::acceptBufferedReadSector(bool replaceExistingData)
{
    if ((m_requestControl & cdrom_detail::REQUEST_ENABLE_BUFFER_READ) == 0 ||
        m_bufferedReadSectors.empty())
    {
        return;
    }

    if (!replaceExistingData && !m_dataFifo.empty())
    {
        return;
    }

    m_dataFifo.clear();
    m_activeSector = std::move(m_bufferedReadSectors.front());
    m_bufferedReadSectors.pop_front();
    m_activeSectorOffset = 0;
    m_dataFifo.pushBackRange(m_activeSector, 0, m_activeSector.size(), DATA_FIFO_CAPACITY);
    m_activeSectorOffset = m_activeSector.size();
    updateDataPadForActiveSector();
}

void Cdrom::loadNextSectorToFifo()
{
    // Enable buffer reads (matching what the real BIOS does before reading
    // sector data) and load the next buffered sector into the data FIFO.
    m_requestControl |= cdrom_detail::REQUEST_ENABLE_BUFFER_READ;
    acceptBufferedReadSector(true);
}

void Cdrom::enableDataRead()
{
    const bool wasEnabled = (m_requestControl & cdrom_detail::REQUEST_ENABLE_BUFFER_READ) != 0;
    m_requestControl |= cdrom_detail::REQUEST_ENABLE_BUFFER_READ;
    if (!wasEnabled)
    {
        // Replicate the BFRD 0→1 rising-edge behaviour from writeRequestControl:
        // load the active sector into the data FIFO so readData() returns valid
        // bytes for the current INT1 without needing a hardware register write.
        if (!m_activeSector.empty())
        {
            m_dataFifo.clear();
            m_activeSectorOffset = 0;
            m_dataFifo.pushBackRange(m_activeSector, 0, m_activeSector.size(), DATA_FIFO_CAPACITY);
            m_activeSectorOffset = m_activeSector.size();
            updateDataPadForActiveSector();
        }
        else
        {
            acceptBufferedReadSector(true);
        }
    }
}

bool Cdrom::queueReadSector()
{
    std::vector<u8> sector;
    if (!loadReadSector(sector))
    {
        return false;
    }

    if (m_bufferedReadSectors.size() >= MAX_BUFFERED_READ_SECTORS)
    {
        m_bufferedReadSectors.pop_front();
    }
    m_bufferedReadSectors.push_back(std::move(sector));
    return true;
}

bool Cdrom::loadReadSector(std::vector<u8>& outSector)
{
    outSector.clear();

    const bool wholeSectorMode = (m_execution.mode & cdrom_detail::SETMODE_SECTOR_SIZE_2340) != 0;
    const bool needRawXa = m_execution.xaStreamingEnabled || m_execution.xaFilterEnabled;

    for (size_t scan = 0; scan < cdrom_detail::MAX_FILTER_SCAN_SECTORS; ++scan)
    {
        std::array<u8, cdrom_detail::RAW_SECTOR_BYTES> rawSector{};
        std::array<u8, cdrom_detail::USER_SECTOR_BYTES> userSector{};
        bool haveRawSector = false;
        bool haveUserSector = false;
        std::array<u8, 8> sectorLoc{};
        bool hasSectorLoc = false;
        u32 loadedLba = m_execution.nextReadLba;

        m_execution.currentLba = loadedLba;
        traceCdrom("loadActiveSector lba=%u scan=%zu mode=0x%02X queue=%zu", loadedLba, scan,
                   m_execution.mode, m_sectorQueue.size());
        if (!m_sectorQueue.empty())
        {
            std::vector<u8> queuedSector = std::move(m_sectorQueue.front());
            m_sectorQueue.pop_front();
            if (queuedSector.size() >= cdrom_detail::RAW_SECTOR_BYTES)
            {
                std::copy_n(queuedSector.begin(), cdrom_detail::RAW_SECTOR_BYTES,
                            rawSector.begin());
                haveRawSector = true;
                std::copy_n(rawSector.begin() +
                                static_cast<std::ptrdiff_t>(cdrom_detail::RAW_SYNC_OFFSET),
                            8, sectorLoc.begin());
                hasSectorLoc = true;
            }
            else
            {
                const size_t copyCount =
                    std::min(queuedSector.size(), cdrom_detail::USER_SECTOR_BYTES);
                std::copy_n(queuedSector.begin(), copyCount, userSector.begin());
                if (copyCount < cdrom_detail::USER_SECTOR_BYTES)
                {
                    std::fill(userSector.begin() + static_cast<std::ptrdiff_t>(copyCount),
                              userSector.end(), 0);
                }
                haveUserSector = true;
            }
            ++m_execution.nextReadLba;
        }
        else if (m_disc != nullptr)
        {
            if (wholeSectorMode || needRawXa)
            {
                haveRawSector = m_disc->readRawSector2352(
                    loadedLba, std::span<u8, cdrom_detail::RAW_SECTOR_BYTES>(rawSector.data(),
                                                                             rawSector.size()));
                if (haveRawSector)
                {
                    std::copy_n(rawSector.begin() +
                                    static_cast<std::ptrdiff_t>(cdrom_detail::RAW_SYNC_OFFSET),
                                8, sectorLoc.begin());
                    hasSectorLoc = true;
                }
            }
            if (!haveRawSector)
            {
                haveUserSector = m_disc->readUserSector(
                    loadedLba, std::span<u8, cdrom_detail::USER_SECTOR_BYTES>(userSector.data(),
                                                                              userSector.size()));
            }
            if (!haveRawSector && !haveUserSector)
            {
                traceCdrom("loadActiveSector read failed lba=%u", loadedLba);
                queueErrorInterrupt(cdrom_detail::ERR_READ_FAIL);
                return false;
            }
            ++m_execution.nextReadLba;
        }
        else
        {
            // Sector queue exhausted with no disc backend — stop reading without
            // issuing an error interrupt so previously buffered sectors are preserved.
            traceCdrom("loadActiveSector queue exhausted, no disc");
            m_execution.readActive = false;
            return false;
        }

        cdrom_detail::XaSubheader xa{};
        const bool hasValidXaSubheader =
            haveRawSector && cdrom_detail::decodeXaSubheader(rawSector, xa);
        const bool isXaForm2 =
            hasValidXaSubheader && (xa.submode & cdrom_detail::XA_SUBMODE_FORM2) != 0;
        const bool isXaAdpcm = isXaForm2 && (xa.submode & cdrom_detail::XA_SUBMODE_AUDIO) != 0 &&
                               (xa.submode & cdrom_detail::XA_SUBMODE_REALTIME) != 0;
        const bool xaFilteredOut = isXaAdpcm && m_execution.xaFilterEnabled &&
                                   (xa.fileNumber != m_execution.xaFilterFile ||
                                    xa.channelNumber != m_execution.xaFilterChannel);
        if (xaFilteredOut)
        {
            traceCdrom("loadActiveSector xa filtered lba=%u file=%u ch=%u", loadedLba,
                       xa.fileNumber, xa.channelNumber);
            continue;
        }

        if (m_xaAudioSink && m_execution.xaStreamingEnabled && isXaAdpcm)
        {
            std::vector<int16_t> xaPcm;
            if (cdrom_detail::decodeXaAudioSector(rawSector, xa, m_execution.xaPrevLeft1,
                                                  m_execution.xaPrevLeft2, m_execution.xaPrevRight1,
                                                  m_execution.xaPrevRight2, xaPcm))
            {
                m_xaAudioSink(xaPcm);
            }
        }

        if (wholeSectorMode)
        {
            if (haveRawSector)
            {
                outSector.assign(rawSector.begin() +
                                     static_cast<std::ptrdiff_t>(cdrom_detail::RAW_SYNC_OFFSET),
                                 rawSector.end());
            }
            else
            {
                outSector.assign(cdrom_detail::WHOLE_SECTOR_BYTES, 0);
                if (haveUserSector)
                {
                    std::copy(userSector.begin(), userSector.end(),
                              outSector.begin() +
                                  static_cast<std::ptrdiff_t>(cdrom_detail::RAW_USER_OFFSET -
                                                              cdrom_detail::RAW_SYNC_OFFSET));
                }
            }
        }
        else if (m_execution.xaStreamingEnabled && isXaForm2)
        {
            outSector.assign(
                rawSector.begin() + static_cast<std::ptrdiff_t>(cdrom_detail::RAW_USER_OFFSET),
                rawSector.begin() + static_cast<std::ptrdiff_t>(cdrom_detail::RAW_USER_OFFSET +
                                                                cdrom_detail::XA_FORM2_USER_BYTES));
        }
        else if (haveRawSector)
        {
            outSector.assign(
                rawSector.begin() + static_cast<std::ptrdiff_t>(cdrom_detail::RAW_USER_OFFSET),
                rawSector.begin() + static_cast<std::ptrdiff_t>(cdrom_detail::RAW_USER_OFFSET +
                                                                cdrom_detail::USER_SECTOR_BYTES));
        }
        else
        {
            outSector.assign(userSector.begin(), userSector.end());
        }

        if (hasSectorLoc)
        {
            std::copy(sectorLoc.begin(), sectorLoc.end(), m_lastGetlocL.begin());
        }
        else if (m_activeSector.size() >= 20)
        {
            m_lastGetlocL[0] = m_activeSector[12];
            m_lastGetlocL[1] = m_activeSector[13];
            m_lastGetlocL[2] = m_activeSector[14];
            m_lastGetlocL[3] = m_activeSector[15];
            m_lastGetlocL[4] = m_activeSector[16];
            m_lastGetlocL[5] = m_activeSector[17];
            m_lastGetlocL[6] = m_activeSector[18];
            m_lastGetlocL[7] = m_activeSector[19];
        }
        else
        {
            u8 minute = 0;
            u8 second = 0;
            u8 frame = 0;
            cdrom_detail::lbaToMsf(m_execution.currentLba, minute, second, frame);
            m_lastGetlocL[0] = minute;
            m_lastGetlocL[1] = second;
            m_lastGetlocL[2] = frame;
            m_lastGetlocL[3] = 0x02;
            m_lastGetlocL[4] = 0x00;
            m_lastGetlocL[5] = 0x00;
            m_lastGetlocL[6] = 0x00;
            m_lastGetlocL[7] = 0x00;
        }

        break;
    }

    if (!outSector.empty())
    {
        traceCdrom("loadActiveSector active_bytes=%zu", outSector.size());
        return true;
    }

    traceCdrom("loadActiveSector no active sector after scan");
    return false;
}

} // namespace runtime
} // namespace psxrecomp
