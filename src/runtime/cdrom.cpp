#include "psxrecomp/runtime/cdrom.h"

#include <algorithm>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u8 INT3 = 1u << 0;
constexpr u8 STATUS_RESPONSE_READY = 1u << 5;
constexpr u8 STATUS_DATA_READY = 1u << 6;

u8 bcdToInt(u8 value)
{
    return static_cast<u8>(((value >> 4) & 0x0F) * 10 + (value & 0x0F));
}

u32 msfToLba(u8 minute, u8 second, u8 frame)
{
    const u32 totalSeconds = static_cast<u32>(bcdToInt(minute)) * 60u + bcdToInt(second);
    return totalSeconds * 75u + bcdToInt(frame);
}
} // namespace

void Cdrom::reset()
{
    m_status = 0;
    m_params.clear();
    m_responses.clear();
    m_dataFifo.clear();
    m_sectorQueue.clear();
    m_activeSector.clear();
    m_activeSectorOffset = 0;
    m_interruptFlags = 0;
    m_interruptEnable = 0;
    m_lastDmaWord = 0;
    m_cyclesUntilSector = CDROM_READ_CYCLES;
    m_readActive = false;
    m_xaStreamingEnabled = false;
}

void Cdrom::tick(u32 cpuCycles)
{
    if (!m_readActive)
    {
        return;
    }

    uint64_t remaining = cpuCycles;
    while (remaining > 0)
    {
        if (remaining < m_cyclesUntilSector)
        {
            m_cyclesUntilSector -= static_cast<u32>(remaining);
            break;
        }

        remaining -= m_cyclesUntilSector;
        m_cyclesUntilSector = CDROM_READ_CYCLES;

        pumpSectorToDataFifo();
        setInterruptFlag(INT3);
    }
}

u8 Cdrom::readStatus() const
{
    u8 status = m_status;
    if (!m_responses.empty())
    {
        status |= STATUS_RESPONSE_READY;
    }
    if (!m_dataFifo.empty())
    {
        status |= STATUS_DATA_READY;
    }
    return status;
}

u8 Cdrom::readData()
{
    if (m_responses.empty())
    {
        return 0;
    }

    u8 value = m_responses.front();
    m_responses.pop_front();
    return value;
}

u8 Cdrom::readInterruptFlags() const
{
    return m_interruptFlags;
}

u8 Cdrom::readInterruptEnable() const
{
    return m_interruptEnable;
}

void Cdrom::writeCommand(u8 value)
{
    m_status = value;
    switch (value)
    {
    case 0x02: // Setloc
        if (m_params.size() >= 3)
        {
            const u8 minute = m_params.front();
            m_params.pop_front();
            const u8 second = m_params.front();
            m_params.pop_front();
            const u8 frame = m_params.front();
            m_params.pop_front();
            (void)msfToLba(minute, second, frame);
        }
        pushResponse(0x00);
        setInterruptFlag(INT3);
        break;
    case 0x06: // ReadN
    case 0x1B: // ReadS
        m_readActive = true;
        m_cyclesUntilSector = CDROM_READ_CYCLES;
        pushResponse(0x00);
        setInterruptFlag(INT3);
        break;
    case 0x09: // Pause
        m_readActive = false;
        pushResponse(0x00);
        setInterruptFlag(INT3);
        break;
    case 0x0A: // Init
        m_readActive = false;
        m_dataFifo.clear();
        m_activeSector.clear();
        m_activeSectorOffset = 0;
        pushResponse(0x00);
        setInterruptFlag(INT3);
        break;
    case 0x0E: // Setmode
        if (!m_params.empty())
        {
            m_xaStreamingEnabled = (m_params.front() & 0x40u) != 0;
            m_params.pop_front();
        }
        pushResponse(0x00);
        setInterruptFlag(INT3);
        break;
    default:
        pushResponse(value);
        setInterruptFlag(INT3);
        break;
    }
}

void Cdrom::writeParam(u8 value)
{
    if (m_params.size() < MAX_PARAMS)
    {
        m_params.push_back(value);
    }
}

void Cdrom::writeInterruptFlags(u8 value)
{
    m_interruptFlags &= static_cast<u8>(~value);
}

void Cdrom::writeInterruptEnable(u8 value)
{
    m_interruptEnable = value;
}

void Cdrom::writeDma(u32 value)
{
    m_lastDmaWord = value;
}

u32 Cdrom::readDma()
{
    u32 value = 0;
    for (u32 i = 0; i < sizeof(u32); ++i)
    {
        const u8 byte = m_dataFifo.empty() ? 0 : m_dataFifo.front();
        if (!m_dataFifo.empty())
        {
            m_dataFifo.pop_front();
        }
        value |= static_cast<u32>(byte) << (i * 8);
    }

    m_lastDmaWord = value;

    if (!m_activeSector.empty())
    {
        pumpSectorToDataFifo();
    }

    return value;
}

u32 Cdrom::lastDmaWord() const
{
    return m_lastDmaWord;
}

void Cdrom::enqueueDataSector(const std::vector<u8>& data)
{
    m_sectorQueue.push_back(data);
}

bool Cdrom::hasIrqRequest() const
{
    return (m_interruptFlags & m_interruptEnable) != 0;
}

void Cdrom::pushResponse(u8 value)
{
    if (m_responses.size() < RESPONSE_CAPACITY)
    {
        m_responses.push_back(value);
    }
}

void Cdrom::setInterruptFlag(u8 mask)
{
    m_interruptFlags |= mask;
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
    const auto first = m_activeSector.begin() + static_cast<std::ptrdiff_t>(m_activeSectorOffset);
    const auto last = first + static_cast<std::ptrdiff_t>(writable);
    m_dataFifo.insert(m_dataFifo.end(), first, last);
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

    if (!m_sectorQueue.empty())
    {
        m_activeSector = std::move(m_sectorQueue.front());
        m_sectorQueue.pop_front();
    }
    else
    {
        m_activeSector.assign(2048, 0);
    }

    m_activeSectorOffset = 0;
    if (m_xaStreamingEnabled && m_activeSector.size() >= 24)
    {
        m_activeSectorOffset = 24;
    }
}

} // namespace runtime
} // namespace psxrecomp
