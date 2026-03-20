#include "psxrecomp/runtime/cdrom.h"

#include "cdrom_shared.h"
#include "psxrecomp/runtime/disc.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u8 XA_SUBMODE_EOR = 0x01;
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

Cdrom::ReadSectorResult Cdrom::queueReadSector()
{
    std::vector<u8> sector;
    m_loadedInt1Record = {};
    m_loadedInt1RecordValid = false;
    const ReadSectorResult result = loadReadSector(sector);
    if (result != ReadSectorResult::BufferedSector)
    {
        return result;
    }

    if (m_bufferedReadSectors.size() >= MAX_BUFFERED_READ_SECTORS)
    {
        m_bufferedReadSectors.pop_front();
        if (!m_bufferedReadLbas.empty())
        {
            m_bufferedReadLbas.pop_front();
        }
    }
    m_bufferedReadSectors.push_back(std::move(sector));
    m_bufferedReadLbas.push_back(m_execution.currentLba);
    if (m_loadedInt1RecordValid)
    {
        m_bufferedInt1Records.push_back(m_loadedInt1Record);
    }
    recordPhaseTrace(m_execution.currentLba, SectorPhaseReason::QueuePromote);
    return ReadSectorResult::BufferedSector;
}

Cdrom::ReadSectorResult Cdrom::loadReadSector(std::vector<u8>& outSector)
{
    outSector.clear();
    bool reachedEndOfStream = false;
    bool readFailure = false;

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
                traceCdrom("loadActiveSector read failed lba=%u scan=%zu", loadedLba, scan);
                const u32 sectorCount = m_disc->userSectorCount();
                if (sectorCount != 0u && loadedLba >= sectorCount)
                {
                    reachedEndOfStream = true;
                }
                else
                {
                    readFailure = true;
                }
                break;
            }
            ++m_execution.nextReadLba;
        }
        else
        {
            traceCdrom("loadActiveSector queue exhausted, no disc");
            m_execution.readActive = false;
            return ReadSectorResult::EndOfStream;
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
            recordPhaseTrace(loadedLba, SectorPhaseReason::FilterReject);
            ++m_filterRejectCount;
            continue;
        }

        if (m_execution.xaStreamingEnabled && isXaAdpcm)
        {
            if (m_xaAudioSink)
            {
                std::vector<int16_t> xaPcm;
                if (cdrom_detail::decodeXaAudioSector(
                        rawSector, xa, m_execution.xaPrevLeft1, m_execution.xaPrevLeft2,
                        m_execution.xaPrevRight1, m_execution.xaPrevRight2, xaPcm))
                {
                    m_xaAudioSink(xaPcm);
                }
            }
            traceCdrom("loadActiveSector xa_audio_deliver lba=%u file=%u ch=%u submode=0x%02X",
                       loadedLba, xa.fileNumber, xa.channelNumber,
                       static_cast<unsigned>(xa.submode));
            recordPhaseTrace(loadedLba, SectorPhaseReason::XaAudioDeliver);
            if (!m_xaPlaybackBusy)
            {
                m_xaPlaybackBusy = true;
                m_xaPlaybackBusyRoseLba = loadedLba;
                m_xaSectorsWhileBusy = 0;
            }
            ++m_xaSectorsWhileBusy;
            m_xaLastCodingInfo = xa.codingInfo;
            ++m_xaDeliveryCount;
            return ReadSectorResult::NoHostData;
        }

        if (m_execution.xaStreamingEnabled && haveRawSector)
        {
            if (!hasValidXaSubheader)
            {
                recordPhaseTrace(loadedLba, SectorPhaseReason::FormatReject);
                ++m_formatRejectCount;
            }
            else if (isXaForm2)
            {
                recordPhaseTrace(loadedLba, SectorPhaseReason::SubmodeReject);
                ++m_submodeRejectCount;
            }
            else
            {
                recordPhaseTrace(m_execution.currentLba, SectorPhaseReason::CpuDataDeliver);
                ++m_cpuDeliveryCount;
            }
        }
        else
        {
            recordPhaseTrace(m_execution.currentLba, SectorPhaseReason::CpuDataDeliver);
            ++m_cpuDeliveryCount;
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

        m_loadedInt1Record = {};
        m_loadedInt1Record.publishedLba = loadedLba;
        m_loadedInt1Record.hasXaSub = hasValidXaSubheader;
        if (hasValidXaSubheader)
        {
            m_loadedInt1Record.xaFile = xa.fileNumber;
            m_loadedInt1Record.xaChannel = xa.channelNumber;
            m_loadedInt1Record.xaSubmode = xa.submode;
            m_loadedInt1Record.xaCoding = xa.codingInfo;
            m_loadedInt1Record.hasEor = (xa.submode & XA_SUBMODE_EOR) != 0;
            m_loadedInt1Record.hasEof = (xa.submode & XA_SUBMODE_EOF) != 0;
        }
        m_loadedInt1RecordValid = true;

        if (hasSectorLoc)
        {
            std::copy(sectorLoc.begin(), sectorLoc.end(), m_lastGetlocL.begin());
        }
        else if (m_activeSector.size() >= 20)
        {
            std::copy_n(m_activeSector.begin() + 12, 8, m_lastGetlocL.begin());
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
        return ReadSectorResult::BufferedSector;
    }

    traceCdrom("loadActiveSector no active sector after scan");
    if (readFailure)
    {
        return ReadSectorResult::ReadFailure;
    }
    return reachedEndOfStream ? ReadSectorResult::EndOfStream : ReadSectorResult::NoHostData;
}

} // namespace runtime
} // namespace psxrecomp
