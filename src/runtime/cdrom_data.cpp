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
    u8 value = 0;
    if (!m_dataFifo.popFront(value))
    {
        return m_dataPadValid ? m_dataPadByte : 0;
    }

    if (!m_activeSector.empty())
    {
        pumpSectorToDataFifo();
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

    const auto consumeDataByte = [this]() -> u8
    {
        u8 byte = 0;
        if (m_dataFifo.popFront(byte))
        {
            if (!m_activeSector.empty())
            {
                pumpSectorToDataFifo();
            }
            return byte;
        }

        if (m_execution.readActive)
        {
            const size_t before = m_dataFifo.size();
            pumpSectorToDataFifo();
            if (m_dataFifo.size() > before)
            {
                traceCdrom("readDma pumped sector fifo_before=%zu fifo_after=%zu lba=%u", before,
                           m_dataFifo.size(), m_execution.currentLba);
                // DMA read consumed a new sector boundary: publish INT1 so
                // polling loops observing CDROM IRQs can progress.
                queueInterruptEvent(cdrom_detail::INT1, {currentStat()});
                if (m_dataFifo.popFront(byte))
                {
                    if (!m_activeSector.empty())
                    {
                        pumpSectorToDataFifo();
                    }
                    return byte;
                }
            }
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

void Cdrom::pumpSectorToDataFifo()
{
    loadActiveSector();
    if (m_activeSector.empty() || m_dataFifo.size() >= DATA_FIFO_CAPACITY)
    {
        return;
    }

    const size_t writable = std::min(DATA_FIFO_CAPACITY - m_dataFifo.size(),
                                     m_activeSector.size() - m_activeSectorOffset);
    m_dataFifo.pushBackRange(m_activeSector, m_activeSectorOffset, writable, DATA_FIFO_CAPACITY);
    m_activeSectorOffset += writable;

    if (m_activeSectorOffset >= m_activeSector.size())
    {
        m_activeSector.clear();
        m_activeSectorOffset = 0;
    }
}

void Cdrom::loadActiveSector()
{
    if (!m_activeSector.empty())
    {
        return;
    }

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
                return;
            }
            ++m_execution.nextReadLba;
        }
        else
        {
            traceCdrom("loadActiveSector no disc");
            queueErrorInterrupt(cdrom_detail::ERR_NO_DISC);
            return;
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
                m_activeSector.assign(
                    rawSector.begin() + static_cast<std::ptrdiff_t>(cdrom_detail::RAW_SYNC_OFFSET),
                    rawSector.end());
            }
            else
            {
                m_activeSector.assign(cdrom_detail::WHOLE_SECTOR_BYTES, 0);
                if (haveUserSector)
                {
                    std::copy(userSector.begin(), userSector.end(),
                              m_activeSector.begin() +
                                  static_cast<std::ptrdiff_t>(cdrom_detail::RAW_USER_OFFSET -
                                                              cdrom_detail::RAW_SYNC_OFFSET));
                }
            }
        }
        else if (m_execution.xaStreamingEnabled && isXaForm2)
        {
            m_activeSector.assign(
                rawSector.begin() + static_cast<std::ptrdiff_t>(cdrom_detail::RAW_USER_OFFSET),
                rawSector.begin() + static_cast<std::ptrdiff_t>(cdrom_detail::RAW_USER_OFFSET +
                                                                cdrom_detail::XA_FORM2_USER_BYTES));
        }
        else if (haveRawSector)
        {
            m_activeSector.assign(
                rawSector.begin() + static_cast<std::ptrdiff_t>(cdrom_detail::RAW_USER_OFFSET),
                rawSector.begin() + static_cast<std::ptrdiff_t>(cdrom_detail::RAW_USER_OFFSET +
                                                                cdrom_detail::USER_SECTOR_BYTES));
        }
        else
        {
            m_activeSector.assign(userSector.begin(), userSector.end());
        }

        m_activeSectorOffset = 0;

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

    if (!m_activeSector.empty())
    {
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
        traceCdrom("loadActiveSector active_bytes=%zu pad=0x%02X", m_activeSector.size(),
                   m_dataPadByte);
    }
    else
    {
        m_dataPadValid = false;
        traceCdrom("loadActiveSector no active sector after scan");
    }
}

} // namespace runtime
} // namespace psxrecomp
