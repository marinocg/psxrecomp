#include "psxrecomp/runtime/cdrom.h"

#include "cdrom_shared.h"

#include <cstdarg>
#include <cstdint>
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
}

void Cdrom::tick(u32 cpuCycles)
{
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

        if (queueReadSector())
        {
            m_execution.seekActive = false;
            m_execution.readActive = true;
            acceptBufferedReadSector(false);
            queueInterruptEvent(cdrom_detail::INT1, {currentStat()});
        }
    }
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
    if (!m_responseFifo.empty() || !m_ackResponseFifo.empty())
    {
        status |= cdrom_detail::STATUS_RESPONSE_READY;
    }
    if ((m_requestControl & cdrom_detail::REQUEST_ENABLE_BUFFER_READ) != 0 && !m_dataFifo.empty())
    {
        status |= cdrom_detail::STATUS_DATA_READY;
    }
    status |= static_cast<u8>(m_status & cdrom_detail::STATUS_COMMAND_BUSY);
    return status;
}

u8 Cdrom::readResponse()
{
    u8 value = 0;
    if (!m_responseFifo.popFront(value))
    {
        // Active FIFO is empty; drain from the ack buffer instead.
        if (!m_ackResponseFifo.popFront(value))
        {
            return 0;
        }
        // When the ack buffer is fully drained, the next queued interrupt can be promoted.
        if (m_ackResponseFifo.empty())
        {
            publishNextInterruptEvent();
            if (canExecutePendingCommand())
            {
                executePendingCommand();
            }
        }
        return value;
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
    snapshot.motorOn = m_execution.motorOn;
    snapshot.readActive = m_execution.readActive;
    snapshot.seekActive = m_execution.seekActive;
    return snapshot;
}

} // namespace runtime
} // namespace psxrecomp
