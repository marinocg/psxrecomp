#pragma once

#include "psxrecomp/types.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <functional>
#include <initializer_list>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class Disc;

class Cdrom
{
  public:
    struct DebugSnapshot
    {
        u8 currentCommand = 0;
        u8 status = 0;
        u8 interruptFlags = 0;
        u8 interruptEnable = 0;
        u8 requestControl = 0;
        u8 mode = 0;
        size_t commandFifoSize = 0;
        size_t responseFifoSize = 0;
        size_t dataFifoSize = 0;
        size_t pendingIrqCount = 0;
        bool motorOn = false;
        bool readActive = false;
        bool seekActive = false;
    };

    void reset();
    void tick(u32 cpuCycles);
    void setDiscBackend(Disc* disc);
    void setXaAudioSink(std::function<void(const std::vector<int16_t>&)> sink);
    void notifyDiscSwap();

    u8 readReg(u8 offset);
    void writeReg(u8 offset, u8 value);

    u8 readStatus() const;
    u8 readResponse();
    u8 readData();
    u8 readInterruptFlags() const;
    u8 readInterruptEnable() const;

    void writeCommand(u8 value);
    void writeParam(u8 value);
    void writeInterruptFlags(u8 value);
    void writeInterruptEnable(u8 value);

    /// Load the next buffered read sector into the data FIFO so that
    /// subsequent readData()/readDma() calls return valid sector data.
    /// Used by the BIOS async-read path after consuming the current sector.
    void loadNextSectorToFifo();

    /// Enable data-buffer reads (set REQUEST_ENABLE_BUFFER_READ) and populate
    /// the data FIFO from the active sector if the bit was previously clear.
    /// Equivalent to the BIOS writing 0x80 to the request register before
    /// reading sector data. Must be called before readData() in the INT1 path.
    void enableDataRead();

    void primeBootState(bool discPresent);

    void writeDma(u32 value);
    u32 readDma();
    u32 lastDmaWord() const;
    DebugSnapshot debugSnapshot() const;

    void enqueueDataSector(const std::vector<u8>& data);
    bool hasIrqRequest() const;
    std::vector<u8> serializeState() const;
    bool deserializeState(const std::vector<u8>& state);

  private:
    struct IrqEvent
    {
        u8 type = 0;
        std::vector<u8> responses;
    };

    struct PendingCommand
    {
        u8 value = 0;
        bool valid = false;
        std::deque<u8> params;

        void clear()
        {
            value = 0;
            valid = false;
            params.clear();
        }
    };

    static constexpr size_t MAX_PARAMS = 16;
    static constexpr size_t RESPONSE_CAPACITY = 32;
    static constexpr size_t DATA_FIFO_CAPACITY = 4096;
    static constexpr size_t MAX_BUFFERED_READ_SECTORS = 8;
    static constexpr size_t MAX_QUEUED_SECTORS = 64;
    static constexpr size_t MAX_QUEUED_IRQ_EVENTS = 32;
    static constexpr u32 CDROM_READ_CYCLES = 451584; // 33.8688MHz / 75 sectors/sec (1x)
    static constexpr u32 CDROM_DOUBLE_SPEED_READ_CYCLES = CDROM_READ_CYCLES / 2u;

    struct CommandFifo
    {
        std::deque<u8> values;

        void clear()
        {
            values.clear();
        }
        bool empty() const
        {
            return values.empty();
        }
        size_t size() const
        {
            return values.size();
        }
        void push(u8 value, size_t maxValues)
        {
            if (values.size() < maxValues)
            {
                values.push_back(value);
            }
        }
        bool popFront(u8& out)
        {
            if (values.empty())
            {
                return false;
            }
            out = values.front();
            values.pop_front();
            return true;
        }
    };

    struct ResponseFifo
    {
        std::deque<u8> values;

        void clear()
        {
            values.clear();
        }
        bool empty() const
        {
            return values.empty();
        }
        void push(u8 value, size_t capacity)
        {
            if (values.size() < capacity)
            {
                values.push_back(value);
            }
        }
        bool popFront(u8& out)
        {
            if (values.empty())
            {
                return false;
            }
            out = values.front();
            values.pop_front();
            return true;
        }
    };

    struct DataFifo
    {
        std::deque<u8> values;

        void clear()
        {
            values.clear();
        }
        bool empty() const
        {
            return values.empty();
        }
        size_t size() const
        {
            return values.size();
        }
        bool popFront(u8& out)
        {
            if (values.empty())
            {
                return false;
            }
            out = values.front();
            values.pop_front();
            return true;
        }
        void pushBackRange(const std::vector<u8>& source, size_t offset, size_t count,
                           size_t capacity)
        {
            if (offset >= source.size() || values.size() >= capacity)
            {
                return;
            }
            const size_t maxWritable = std::min(source.size() - offset, capacity - values.size());
            const size_t writable = std::min(count, maxWritable);
            if (writable == 0)
            {
                return;
            }
            const auto begin = source.begin() + static_cast<std::ptrdiff_t>(offset);
            values.insert(values.end(), begin, begin + static_cast<std::ptrdiff_t>(writable));
        }
    };

    struct CommandExecutionState
    {
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

        void reset(u32 readCycles)
        {
            currentCommand = 0;
            mode = 0;
            xaFilterFile = 0;
            xaFilterChannel = 0;
            nextReadLba = 0;
            currentLba = 0;
            cyclesUntilSector = readCycles;
            xaPrevLeft1 = 0;
            xaPrevLeft2 = 0;
            xaPrevRight1 = 0;
            xaPrevRight2 = 0;
            motorOn = false;
            readActive = false;
            seekActive = false;
            xaStreamingEnabled = false;
            xaFilterEnabled = false;
            pendingResponseIrqs.clear();
        }
    };

    u8 m_status = 0;
    u8 m_index = 0;
    CommandFifo m_commandFifo;
    ResponseFifo m_responseFifo;
    ResponseFifo m_ackResponseFifo;
    DataFifo m_dataFifo;
    CommandExecutionState m_execution;
    std::deque<std::vector<u8>> m_sectorQueue;
    std::deque<std::vector<u8>> m_bufferedReadSectors;
    std::vector<u8> m_activeSector;
    size_t m_activeSectorOffset = 0;
    u8 m_interruptFlags = 0;
    u8 m_interruptEnable = 0;
    u8 m_requestControl = 0;
    PendingCommand m_pendingCommand;
    u32 m_lastDmaWord = 0;
    Disc* m_disc = nullptr;
    std::function<void(const std::vector<int16_t>&)> m_xaAudioSink;
    std::array<u8, 8> m_lastGetlocL{};
    u8 m_dataPadByte = 0;
    bool m_dataPadValid = false;
    bool m_discBackendInitialized = false;
    bool m_doorOpen = false;
    u32 m_doorCloseCycles = 0;

    void pushResponse(u8 value);
    u8 currentStat() const;
    void queueErrorInterrupt(u8 reasonCode);
    void beginDoorOpenTransition(bool closeAfterTransition);
    void writeRequestControl(u8 value);
    bool canExecutePendingCommand() const;
    void enqueueCommand(u8 value);
    void executePendingCommand();
    void queueInterruptEvent(u8 type, std::initializer_list<u8> responses = {});
    void publishNextInterruptEvent();
    void acceptBufferedReadSector(bool replaceExistingData);
    bool queueReadSector();
    bool loadReadSector(std::vector<u8>& outSector);
    u32 currentReadCycles() const;
};

} // namespace runtime
} // namespace psxrecomp
