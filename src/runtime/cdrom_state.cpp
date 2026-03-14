#include "psxrecomp/runtime/cdrom.h"

#include "cdrom_shared.h"

#include <array>
#include <cstdint>

namespace psxrecomp
{
namespace runtime
{

namespace
{
void appendU8(std::vector<u8>& out, u8 value)
{
    out.push_back(value);
}

void appendBool(std::vector<u8>& out, bool value)
{
    appendU8(out, value ? 1u : 0u);
}

void appendU32(std::vector<u8>& out, u32 value)
{
    out.push_back(static_cast<u8>(value & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
}

void appendS32(std::vector<u8>& out, int value)
{
    appendU32(out, static_cast<u32>(static_cast<int32_t>(value)));
}

void appendByteVector(std::vector<u8>& out, const std::vector<u8>& values)
{
    appendU32(out, static_cast<u32>(values.size()));
    out.insert(out.end(), values.begin(), values.end());
}

void appendByteDeque(std::vector<u8>& out, const std::deque<u8>& values)
{
    appendU32(out, static_cast<u32>(values.size()));
    out.insert(out.end(), values.begin(), values.end());
}

bool consumeU8(const std::vector<u8>& data, size_t& cursor, u8& out)
{
    if (cursor >= data.size())
    {
        return false;
    }
    out = data[cursor];
    ++cursor;
    return true;
}

bool consumeBool(const std::vector<u8>& data, size_t& cursor, bool& out)
{
    u8 raw = 0;
    if (!consumeU8(data, cursor, raw) || raw > 1u)
    {
        return false;
    }
    out = raw != 0;
    return true;
}

bool consumeU32(const std::vector<u8>& data, size_t& cursor, u32& out)
{
    if (cursor + sizeof(u32) > data.size())
    {
        return false;
    }
    out = static_cast<u32>(data[cursor]) | (static_cast<u32>(data[cursor + 1]) << 8u) |
          (static_cast<u32>(data[cursor + 2]) << 16u) | (static_cast<u32>(data[cursor + 3]) << 24u);
    cursor += sizeof(u32);
    return true;
}

bool consumeS32(const std::vector<u8>& data, size_t& cursor, int& out)
{
    u32 raw = 0;
    if (!consumeU32(data, cursor, raw))
    {
        return false;
    }
    out = static_cast<int>(static_cast<int32_t>(raw));
    return true;
}

bool consumeByteVector(const std::vector<u8>& data, size_t& cursor, size_t maxValues,
                       std::vector<u8>& out)
{
    u32 count = 0;
    if (!consumeU32(data, cursor, count))
    {
        return false;
    }
    if (count > maxValues || cursor + count > data.size())
    {
        return false;
    }
    out.assign(data.begin() + static_cast<std::ptrdiff_t>(cursor),
               data.begin() + static_cast<std::ptrdiff_t>(cursor + count));
    cursor += count;
    return true;
}

bool consumeByteDeque(const std::vector<u8>& data, size_t& cursor, size_t maxValues,
                      std::deque<u8>& out)
{
    std::vector<u8> values;
    if (!consumeByteVector(data, cursor, maxValues, values))
    {
        return false;
    }
    out.assign(values.begin(), values.end());
    return true;
}
} // namespace

std::vector<u8> Cdrom::serializeState() const
{
    std::vector<u8> out;
    out.reserve(8192);

    appendU32(out, cdrom_detail::CDROM_STATE_MAGIC);
    appendU32(out, cdrom_detail::CDROM_STATE_VERSION);

    appendU8(out, m_status);
    appendU8(out, m_index);
    appendU8(out, m_interruptFlags);
    appendU8(out, m_interruptEnable);
    appendU8(out, m_requestControl);
    appendBool(out, m_pendingCommand.valid);
    appendU8(out, m_pendingCommand.value);
    appendByteDeque(out, m_pendingCommand.params);
    appendU32(out, m_lastDmaWord);
    appendU8(out, m_dataPadByte);
    appendBool(out, m_dataPadValid);
    appendBool(out, m_discBackendInitialized);
    appendBool(out, m_doorOpen);
    appendU32(out, m_doorCloseCycles);
    for (u8 value : m_lastGetlocL)
    {
        appendU8(out, value);
    }

    appendByteDeque(out, m_commandFifo.values);
    appendByteDeque(out, m_responseFifo.values);
    appendByteDeque(out, m_ackResponseFifo.values);
    appendByteDeque(out, m_dataFifo.values);

    appendU8(out, m_execution.currentCommand);
    appendU8(out, m_execution.mode);
    appendU8(out, m_execution.xaFilterFile);
    appendU8(out, m_execution.xaFilterChannel);
    appendU32(out, m_execution.nextReadLba);
    appendU32(out, m_execution.currentLba);
    appendU32(out, m_execution.cyclesUntilSector);
    appendS32(out, m_execution.xaPrevLeft1);
    appendS32(out, m_execution.xaPrevLeft2);
    appendS32(out, m_execution.xaPrevRight1);
    appendS32(out, m_execution.xaPrevRight2);
    appendBool(out, m_execution.motorOn);
    appendBool(out, m_execution.readActive);
    appendBool(out, m_execution.seekActive);
    appendBool(out, m_execution.xaStreamingEnabled);
    appendBool(out, m_execution.xaFilterEnabled);

    appendU32(out, static_cast<u32>(m_execution.pendingResponseIrqs.size()));
    for (const IrqEvent& event : m_execution.pendingResponseIrqs)
    {
        appendU8(out, event.type);
        appendByteVector(out, event.responses);
    }

    appendU32(out, static_cast<u32>(m_sectorQueue.size()));
    for (const std::vector<u8>& sector : m_sectorQueue)
    {
        appendByteVector(out, sector);
    }

    appendU32(out, static_cast<u32>(m_bufferedReadSectors.size()));
    for (const std::vector<u8>& sector : m_bufferedReadSectors)
    {
        appendByteVector(out, sector);
    }

    appendByteVector(out, m_activeSector);
    appendU32(out, static_cast<u32>(m_activeSectorOffset));

    return out;
}

bool Cdrom::deserializeState(const std::vector<u8>& state)
{
    size_t cursor = 0;
    u32 magic = 0;
    u32 version = 0;
    u8 status = 0;
    u8 index = 0;
    u8 interruptFlags = 0;
    u8 interruptEnable = 0;
    u8 requestControl = 0;
    bool pendingCommandValid = false;
    u8 pendingCommandValue = 0;
    std::deque<u8> pendingCommandParams;
    u32 lastDmaWord = 0;
    u8 dataPadByte = 0;
    bool dataPadValid = false;
    bool discBackendInitialized = false;
    bool doorOpen = false;
    u32 doorCloseCycles = 0;
    std::array<u8, 8> lastGetlocL{};
    std::deque<u8> commandValues;
    std::deque<u8> responseValues;
    std::deque<u8> ackResponseValues;
    std::deque<u8> dataValues;
    u8 currentCommand = 0;
    u8 mode = 0;
    u8 xaFilterFile = 0;
    u8 xaFilterChannel = 0;
    u32 nextReadLba = 0;
    u32 currentLba = 0;
    u32 cyclesUntilSector = 0;
    int xaPrevLeft1 = 0;
    int xaPrevLeft2 = 0;
    int xaPrevRight1 = 0;
    int xaPrevRight2 = 0;
    bool motorOn = false;
    bool readActive = false;
    bool seekActive = false;
    bool xaStreamingEnabled = false;
    bool xaFilterEnabled = false;
    std::deque<IrqEvent> pendingResponseIrqs;
    std::deque<std::vector<u8>> sectorQueue;
    std::deque<std::vector<u8>> bufferedReadSectors;
    std::vector<u8> activeSector;
    u32 activeSectorOffset = 0;

    if (!consumeU32(state, cursor, magic) || !consumeU32(state, cursor, version) ||
        magic != cdrom_detail::CDROM_STATE_MAGIC || version != cdrom_detail::CDROM_STATE_VERSION ||
        !consumeU8(state, cursor, status) || !consumeU8(state, cursor, index) ||
        !consumeU8(state, cursor, interruptFlags) || !consumeU8(state, cursor, interruptEnable) ||
        !consumeU8(state, cursor, requestControl) ||
        !consumeBool(state, cursor, pendingCommandValid) ||
        !consumeU8(state, cursor, pendingCommandValue) ||
        !consumeByteDeque(state, cursor, MAX_PARAMS, pendingCommandParams) ||
        !consumeU32(state, cursor, lastDmaWord) || !consumeU8(state, cursor, dataPadByte) ||
        !consumeBool(state, cursor, dataPadValid) ||
        !consumeBool(state, cursor, discBackendInitialized) ||
        !consumeBool(state, cursor, doorOpen) || !consumeU32(state, cursor, doorCloseCycles))
    {
        return false;
    }

    for (u8& value : lastGetlocL)
    {
        if (!consumeU8(state, cursor, value))
        {
            return false;
        }
    }

    if (!consumeByteDeque(state, cursor, MAX_PARAMS, commandValues) ||
        !consumeByteDeque(state, cursor, RESPONSE_CAPACITY, responseValues) ||
        !consumeByteDeque(state, cursor, RESPONSE_CAPACITY, ackResponseValues) ||
        !consumeByteDeque(state, cursor, DATA_FIFO_CAPACITY, dataValues) ||
        !consumeU8(state, cursor, currentCommand) || !consumeU8(state, cursor, mode) ||
        !consumeU8(state, cursor, xaFilterFile) || !consumeU8(state, cursor, xaFilterChannel) ||
        !consumeU32(state, cursor, nextReadLba) || !consumeU32(state, cursor, currentLba) ||
        !consumeU32(state, cursor, cyclesUntilSector) || !consumeS32(state, cursor, xaPrevLeft1) ||
        !consumeS32(state, cursor, xaPrevLeft2) || !consumeS32(state, cursor, xaPrevRight1) ||
        !consumeS32(state, cursor, xaPrevRight2) || !consumeBool(state, cursor, motorOn) ||
        !consumeBool(state, cursor, readActive) || !consumeBool(state, cursor, seekActive) ||
        !consumeBool(state, cursor, xaStreamingEnabled) ||
        !consumeBool(state, cursor, xaFilterEnabled))
    {
        return false;
    }

    u32 irqEventCount = 0;
    if (!consumeU32(state, cursor, irqEventCount) || irqEventCount > MAX_QUEUED_IRQ_EVENTS)
    {
        return false;
    }
    for (u32 i = 0; i < irqEventCount; ++i)
    {
        IrqEvent event;
        if (!consumeU8(state, cursor, event.type) || (event.type & 0x07u) == 0 ||
            !consumeByteVector(state, cursor, RESPONSE_CAPACITY, event.responses))
        {
            return false;
        }
        event.type &= 0x07u;
        pendingResponseIrqs.push_back(std::move(event));
    }

    u32 queuedSectorCount = 0;
    if (!consumeU32(state, cursor, queuedSectorCount) || queuedSectorCount > MAX_QUEUED_SECTORS)
    {
        return false;
    }
    for (u32 i = 0; i < queuedSectorCount; ++i)
    {
        std::vector<u8> sector;
        if (!consumeByteVector(state, cursor, cdrom_detail::MAX_SERIALIZED_SECTOR_BYTES, sector))
        {
            return false;
        }
        sectorQueue.push_back(std::move(sector));
    }

    u32 bufferedReadSectorCount = 0;
    if (!consumeU32(state, cursor, bufferedReadSectorCount) ||
        bufferedReadSectorCount > MAX_BUFFERED_READ_SECTORS)
    {
        return false;
    }
    for (u32 i = 0; i < bufferedReadSectorCount; ++i)
    {
        std::vector<u8> sector;
        if (!consumeByteVector(state, cursor, cdrom_detail::MAX_SERIALIZED_SECTOR_BYTES, sector))
        {
            return false;
        }
        bufferedReadSectors.push_back(std::move(sector));
    }

    if (!consumeByteVector(state, cursor, cdrom_detail::MAX_SERIALIZED_SECTOR_BYTES,
                           activeSector) ||
        !consumeU32(state, cursor, activeSectorOffset) || cursor != state.size())
    {
        return false;
    }

    if (index > 3u || activeSectorOffset > activeSector.size())
    {
        return false;
    }

    m_status = status;
    m_index = index;
    m_interruptFlags = interruptFlags;
    m_interruptEnable = static_cast<u8>(interruptEnable & 0x1Fu);
    m_requestControl = static_cast<u8>(requestControl & 0xE0u);
    m_pendingCommand.valid = pendingCommandValid;
    m_pendingCommand.value = pendingCommandValue;
    m_pendingCommand.params = std::move(pendingCommandParams);
    m_lastDmaWord = lastDmaWord;
    m_dataPadByte = dataPadByte;
    m_dataPadValid = dataPadValid;
    m_discBackendInitialized = discBackendInitialized;
    m_doorOpen = doorOpen;
    m_doorCloseCycles = doorCloseCycles;
    m_lastGetlocL = lastGetlocL;
    m_commandFifo.values = std::move(commandValues);
    m_responseFifo.values = std::move(responseValues);
    m_ackResponseFifo.values = std::move(ackResponseValues);
    m_dataFifo.values = std::move(dataValues);
    m_execution.currentCommand = currentCommand;
    m_execution.mode = mode;
    m_execution.xaFilterFile = xaFilterFile;
    m_execution.xaFilterChannel = xaFilterChannel;
    m_execution.nextReadLba = nextReadLba;
    m_execution.currentLba = currentLba;
    m_execution.cyclesUntilSector = cyclesUntilSector;
    m_execution.xaPrevLeft1 = xaPrevLeft1;
    m_execution.xaPrevLeft2 = xaPrevLeft2;
    m_execution.xaPrevRight1 = xaPrevRight1;
    m_execution.xaPrevRight2 = xaPrevRight2;
    m_execution.motorOn = motorOn;
    m_execution.readActive = readActive;
    m_execution.seekActive = seekActive;
    m_execution.xaStreamingEnabled = xaStreamingEnabled;
    m_execution.xaFilterEnabled = xaFilterEnabled;
    m_execution.pendingResponseIrqs = std::move(pendingResponseIrqs);
    m_sectorQueue = std::move(sectorQueue);
    m_bufferedReadSectors = std::move(bufferedReadSectors);
    m_activeSector = std::move(activeSector);
    m_activeSectorOffset = static_cast<size_t>(activeSectorOffset);
    return true;
}

} // namespace runtime
} // namespace psxrecomp
