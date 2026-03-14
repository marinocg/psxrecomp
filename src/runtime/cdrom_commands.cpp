#include "psxrecomp/runtime/cdrom.h"

#include "cdrom_shared.h"
#include "psxrecomp/runtime/disc.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

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

bool Cdrom::canExecutePendingCommand() const
{
    return m_pendingCommand.valid && (m_interruptFlags & 0x07u) == 0u && m_responseFifo.empty() &&
           m_execution.pendingResponseIrqs.empty();
}

void Cdrom::enqueueCommand(u8 value)
{
    m_pendingCommand.value = value;
    m_pendingCommand.valid = true;
    m_pendingCommand.params = std::move(m_commandFifo.values);
    m_commandFifo.clear();
    m_execution.currentCommand = value;
}

void Cdrom::writeCommand(u8 value)
{
    traceCdrom("writeCommand cmd=0x%02X idx=%u params=%zu irq=0x%02X pending=%zu", value, m_index,
               m_commandFifo.size(), m_interruptFlags, m_execution.pendingResponseIrqs.size());
    enqueueCommand(value);
    if (canExecutePendingCommand())
    {
        executePendingCommand();
    }
    traceCdrom("writeCommand queued cmd=0x%02X irq=0x%02X resp=%zu pending=%zu data=%zu", value,
               m_interruptFlags, m_responseFifo.values.size(),
               m_execution.pendingResponseIrqs.size(), m_dataFifo.size());
}

void Cdrom::executePendingCommand()
{
    if (!m_pendingCommand.valid)
    {
        return;
    }

    const u8 value = m_pendingCommand.value;
    std::deque<u8> params = std::move(m_pendingCommand.params);
    m_pendingCommand.clear();

    auto popParam = [&params](u8& out) -> bool
    {
        if (params.empty())
        {
            return false;
        }
        out = params.front();
        params.pop_front();
        return true;
    };

    switch (value)
    {
    case 0x01: // Getstat
        queueInterruptEvent(cdrom_detail::INT3, {currentStat()});
        break;
    case 0x02: // Setloc
        if (params.size() >= 3)
        {
            u8 minute = 0;
            u8 second = 0;
            u8 frame = 0;
            (void)popParam(minute);
            (void)popParam(second);
            (void)popParam(frame);
            m_execution.nextReadLba = cdrom_detail::msfToLba(minute, second, frame);
            m_execution.currentLba = m_execution.nextReadLba;
            traceCdrom("Setloc params=%02X:%02X:%02X -> lba=%u", minute, second, frame,
                       m_execution.nextReadLba);
            m_execution.xaPrevLeft1 = 0;
            m_execution.xaPrevLeft2 = 0;
            m_execution.xaPrevRight1 = 0;
            m_execution.xaPrevRight2 = 0;
            m_lastGetlocL[0] = minute;
            m_lastGetlocL[1] = second;
            m_lastGetlocL[2] = frame;
            m_lastGetlocL[3] = 0x02;
            m_lastGetlocL[4] = 0x00;
            m_lastGetlocL[5] = 0x00;
            m_lastGetlocL[6] = 0x00;
            m_lastGetlocL[7] = 0x00;
        }
        queueInterruptEvent(cdrom_detail::INT3, {currentStat()});
        break;
    case 0x06: // ReadN
    case 0x1B: // ReadS
        if (m_doorOpen || (m_disc == nullptr && m_sectorQueue.empty()))
        {
            queueErrorInterrupt(cdrom_detail::ERR_NO_DISC);
            break;
        }
        m_execution.motorOn = true;
        m_execution.currentLba = m_execution.nextReadLba;
        m_execution.readActive = false;
        m_execution.seekActive = true;
        m_execution.cyclesUntilSector = currentReadCycles();
        m_dataFifo.clear();
        m_bufferedReadSectors.clear();
        m_activeSector.clear();
        m_activeSectorOffset = 0;
        m_dataPadValid = false;
        queueInterruptEvent(cdrom_detail::INT3, {currentStat()});
        break;
    case 0x08: // Stop
    {
        const u8 firstStat = static_cast<u8>(currentStat() & ~cdrom_detail::STAT_READ_ACTIVE);
        m_execution.readActive = false;
        m_execution.seekActive = false;
        m_execution.xaPrevLeft1 = 0;
        m_execution.xaPrevLeft2 = 0;
        m_execution.xaPrevRight1 = 0;
        m_execution.xaPrevRight2 = 0;
        m_dataFifo.clear();
        m_bufferedReadSectors.clear();
        m_activeSector.clear();
        m_activeSectorOffset = 0;
        m_dataPadValid = false;
        queueInterruptEvent(cdrom_detail::INT3, {firstStat});
        m_execution.motorOn = false;
        queueInterruptEvent(cdrom_detail::INT2, {currentStat()});
        break;
    }
    case 0x09: // Pause
    {
        const u8 firstStat = currentStat();
        m_execution.readActive = false;
        m_execution.seekActive = false;
        m_execution.xaPrevLeft1 = 0;
        m_execution.xaPrevLeft2 = 0;
        m_execution.xaPrevRight1 = 0;
        m_execution.xaPrevRight2 = 0;
        m_dataFifo.clear();
        m_bufferedReadSectors.clear();
        m_activeSector.clear();
        m_activeSectorOffset = 0;
        m_dataPadValid = false;
        queueInterruptEvent(cdrom_detail::INT3, {firstStat});
        queueInterruptEvent(cdrom_detail::INT2, {currentStat()});
        break;
    }
    case 0x0A: // Init
        m_execution.mode = 0x20;
        m_execution.motorOn = true;
        m_execution.readActive = false;
        m_execution.seekActive = false;
        m_execution.xaStreamingEnabled = false;
        m_execution.xaFilterEnabled = false;
        m_execution.xaFilterFile = 0;
        m_execution.xaFilterChannel = 0;
        m_execution.nextReadLba = 0;
        m_execution.currentLba = 0;
        m_execution.xaPrevLeft1 = 0;
        m_execution.xaPrevLeft2 = 0;
        m_execution.xaPrevRight1 = 0;
        m_execution.xaPrevRight2 = 0;
        m_dataFifo.clear();
        m_bufferedReadSectors.clear();
        m_activeSector.clear();
        m_activeSectorOffset = 0;
        m_dataPadValid = false;
        queueInterruptEvent(cdrom_detail::INT3, {currentStat()});
        queueInterruptEvent(cdrom_detail::INT2, {currentStat()});
        break;
    case 0x0B: // Mute
    case 0x0C: // Demute
        queueInterruptEvent(cdrom_detail::INT3, {currentStat()});
        break;
    case 0x0D: // Setfilter
    {
        u8 file = 0;
        u8 channel = 0;
        (void)popParam(file);
        (void)popParam(channel);
        m_execution.xaFilterFile = file;
        m_execution.xaFilterChannel = channel;
        queueInterruptEvent(cdrom_detail::INT3, {currentStat()});
        break;
    }
    case 0x10: // GetlocL
        queueInterruptEvent(cdrom_detail::INT3,
                            {m_lastGetlocL[0], m_lastGetlocL[1], m_lastGetlocL[2], m_lastGetlocL[3],
                             m_lastGetlocL[4], m_lastGetlocL[5], m_lastGetlocL[6],
                             m_lastGetlocL[7]});
        break;
    case 0x11: // GetlocP
    {
        u8 relMinute = 0;
        u8 relSecond = 0;
        u8 relFrame = 0;
        u8 absMinute = 0;
        u8 absSecond = 0;
        u8 absFrame = 0;
        cdrom_detail::lbaToMsf(m_execution.currentLba, absMinute, absSecond, absFrame);
        cdrom_detail::lbaToRelativeMsf(m_execution.currentLba, relMinute, relSecond, relFrame);
        queueInterruptEvent(cdrom_detail::INT3, {0x01, 0x01, relMinute, relSecond, relFrame,
                                                 absMinute, absSecond, absFrame});
        break;
    }
    case 0x13: // GetTN
        queueInterruptEvent(cdrom_detail::INT3, {currentStat(), 0x01, 0x01});
        break;
    case 0x14: // GetTD
    {
        u8 track = 0;
        (void)popParam(track);
        u8 minute = 0;
        u8 second = 0;
        if (cdrom_detail::bcdToInt(track) == 0)
        {
            u32 leadOutLba = 0;
            if (m_disc != nullptr)
            {
                leadOutLba = m_disc->userSectorCount();
            }
            u8 frame = 0;
            cdrom_detail::lbaToMsf(leadOutLba, minute, second, frame);
        }
        else
        {
            minute = 0x00;
            second = 0x02;
        }
        queueInterruptEvent(cdrom_detail::INT3, {currentStat(), minute, second});
        break;
    }
    case 0x15: // SeekL
    {
        m_execution.motorOn = true;
        m_execution.readActive = false;
        m_execution.seekActive = true;
        queueInterruptEvent(cdrom_detail::INT3, {currentStat()});
        m_execution.seekActive = false;
        m_execution.currentLba = m_execution.nextReadLba;
        m_execution.xaPrevLeft1 = 0;
        m_execution.xaPrevLeft2 = 0;
        m_execution.xaPrevRight1 = 0;
        m_execution.xaPrevRight2 = 0;
        queueInterruptEvent(cdrom_detail::INT2, {currentStat()});
        break;
    }
    case 0x1A: // GetID
    {
        queueInterruptEvent(cdrom_detail::INT3, {currentStat()});
        const bool discPresent = !m_doorOpen && ((m_disc != nullptr) || !m_sectorQueue.empty() ||
                                                 !m_activeSector.empty());
        if (discPresent)
        {
            const u8 stat = static_cast<u8>(currentStat() & ~cdrom_detail::STAT_ID_ERROR);
            queueInterruptEvent(cdrom_detail::INT2, {stat, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'A'});
        }
        else
        {
            const u8 stat = static_cast<u8>(currentStat() | cdrom_detail::STAT_ID_ERROR);
            queueInterruptEvent(cdrom_detail::INT5, {stat, cdrom_detail::ERR_NO_DISC, 0x00, 0x00,
                                                     0x00, 0x00, 0x00, 0x00});
        }
        break;
    }
    case 0x0E: // Setmode
    {
        u8 mode = 0;
        if (popParam(mode))
        {
            m_execution.mode = mode;
            m_execution.xaStreamingEnabled = (mode & cdrom_detail::SETMODE_XA_STREAM_ENABLE) != 0;
            m_execution.xaFilterEnabled = (mode & cdrom_detail::SETMODE_XA_FILTER_ENABLE) != 0;
        }
        queueInterruptEvent(cdrom_detail::INT3, {currentStat()});
        break;
    }
    default:
        queueInterruptEvent(cdrom_detail::INT3, {value});
        break;
    }

    traceCdrom("executePendingCommand done cmd=0x%02X irq=0x%02X resp=%zu pending=%zu data=%zu",
               value, m_interruptFlags, m_responseFifo.values.size(),
               m_execution.pendingResponseIrqs.size(), m_dataFifo.size());
}

void Cdrom::writeParam(u8 value)
{
    m_commandFifo.push(value, MAX_PARAMS);
}

void Cdrom::writeInterruptFlags(u8 value)
{
    traceCdrom("writeInterruptFlags value=0x%02X before irq=0x%02X resp=%zu pending=%zu", value,
               m_interruptFlags, m_responseFifo.values.size(),
               m_execution.pendingResponseIrqs.size());
    const u8 ackMask = static_cast<u8>(value & 0x1Fu);
    const u8 currentType = static_cast<u8>(m_interruptFlags & 0x07u);
    if (ackMask != 0 && currentType >= 1u && currentType <= 5u)
    {
        const u8 currentTypeBit = static_cast<u8>(1u << (currentType - 1u));
        if ((ackMask & currentTypeBit) != 0u)
        {
            m_interruptFlags = static_cast<u8>(m_interruptFlags & 0xF8u);
            m_ackResponseFifo.clear();
            publishNextInterruptEvent();
            if (canExecutePendingCommand())
            {
                executePendingCommand();
            }
        }
    }

    if ((value & 0x40u) != 0)
    {
        m_commandFifo.clear();
    }
    traceCdrom("writeInterruptFlags after irq=0x%02X resp=%zu pending=%zu", m_interruptFlags,
               m_responseFifo.values.size(), m_execution.pendingResponseIrqs.size());
}

void Cdrom::writeRequestControl(u8 value)
{
    m_requestControl = static_cast<u8>(value & 0xE0u);
    traceCdrom("writeRequestControl value=0x%02X req=0x%02X data=%zu active=%zu", value,
               m_requestControl, m_dataFifo.size(), m_activeSector.size());
    if ((m_requestControl & cdrom_detail::REQUEST_ENABLE_BUFFER_READ) != 0)
    {
        acceptBufferedReadSector(true);
    }
}

void Cdrom::writeInterruptEnable(u8 value)
{
    m_interruptEnable = static_cast<u8>(value & 0x1Fu);
}

bool Cdrom::hasIrqRequest() const
{
    const u8 currentType = static_cast<u8>(m_interruptFlags & 0x07u);
    if (currentType == 0u || currentType > 5u)
    {
        return false;
    }
    const u8 currentTypeBit = static_cast<u8>(1u << (currentType - 1u));
    return (m_interruptEnable & currentTypeBit) != 0u;
}

void Cdrom::pushResponse(u8 value)
{
    m_responseFifo.push(value, RESPONSE_CAPACITY);
}

u8 Cdrom::currentStat() const
{
    u8 stat = 0;
    if (m_execution.motorOn)
    {
        stat |= cdrom_detail::STAT_MOTOR_ON;
    }
    if (m_doorOpen)
    {
        stat |= cdrom_detail::STAT_SHELL_OPEN;
    }
    if (m_execution.readActive)
    {
        stat |= cdrom_detail::STAT_READ_ACTIVE;
    }
    if (m_execution.seekActive)
    {
        stat |= cdrom_detail::STAT_SEEK_ACTIVE;
    }
    return stat;
}

void Cdrom::queueErrorInterrupt(u8 reasonCode)
{
    traceCdrom("queueErrorInterrupt reason=0x%02X", reasonCode);
    m_execution.readActive = false;
    m_execution.seekActive = false;
    m_dataFifo.clear();
    m_bufferedReadSectors.clear();
    m_activeSector.clear();
    m_activeSectorOffset = 0;
    m_dataPadValid = false;
    const u8 stat = static_cast<u8>(currentStat() | cdrom_detail::STAT_ID_ERROR);
    queueInterruptEvent(cdrom_detail::INT5, {stat, reasonCode, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

void Cdrom::beginDoorOpenTransition(bool closeAfterTransition)
{
    m_execution.motorOn = false;
    m_doorOpen = true;
    m_doorCloseCycles = closeAfterTransition ? cdrom_detail::DOOR_CLOSE_TRANSITION_CYCLES : 0;
    m_execution.readActive = false;
    m_execution.seekActive = false;
    m_dataFifo.clear();
    m_bufferedReadSectors.clear();
    m_activeSector.clear();
    m_activeSectorOffset = 0;
    m_dataPadValid = false;
}

void Cdrom::queueInterruptEvent(u8 type, std::initializer_list<u8> responses)
{
    const u8 irqType = static_cast<u8>(type & 0x07u);
    if (irqType == 0)
    {
        return;
    }

    if (m_execution.pendingResponseIrqs.size() >= MAX_QUEUED_IRQ_EVENTS)
    {
        m_execution.pendingResponseIrqs.pop_front();
    }

    IrqEvent event;
    event.type = irqType;
    event.responses.assign(responses.begin(), responses.end());
    m_execution.pendingResponseIrqs.push_back(std::move(event));
    traceCdrom("queueInterruptEvent type=%u responses=%zu pending=%zu", irqType, responses.size(),
               m_execution.pendingResponseIrqs.size());
    publishNextInterruptEvent();
}

void Cdrom::publishNextInterruptEvent()
{
    if ((m_interruptFlags & 0x07u) != 0 || !m_responseFifo.empty() ||
        m_execution.pendingResponseIrqs.empty())
    {
        return;
    }

    IrqEvent event = std::move(m_execution.pendingResponseIrqs.front());
    m_execution.pendingResponseIrqs.pop_front();
    m_interruptFlags = static_cast<u8>((m_interruptFlags & 0xF8u) | (event.type & 0x07u));
    m_responseFifo.clear();
    for (u8 byte : event.responses)
    {
        pushResponse(byte);
    }
    if (!event.responses.empty())
    {
        traceCdrom("publishNextInterruptEvent type=%u resp=%zu first=0x%02X pending=%zu",
                   event.type, m_responseFifo.values.size(), event.responses.front(),
                   m_execution.pendingResponseIrqs.size());
    }
    else
    {
        traceCdrom("publishNextInterruptEvent type=%u resp=%zu pending=%zu", event.type,
                   m_responseFifo.values.size(), m_execution.pendingResponseIrqs.size());
    }
}

} // namespace runtime
} // namespace psxrecomp
