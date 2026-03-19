#pragma once

#include "psxrecomp/types.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <functional>
#include <initializer_list>
#include <string>
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

    /// Reasons that can trigger a sector-phase transition event.
    enum class SectorPhaseReason : u8
    {
        QueuePromote,   ///< Sector read from disc/queue; now in m_bufferedReadSectors (buffered).
        PublishInt3,    ///< INT3 (command-complete) published to the interrupt register.
        PublishInt1,    ///< INT1 (data-ready) published; m_activeSector advances (published).
        HclrctlAck,    ///< HCLRCTL write acknowledged and cleared the active interrupt.
        AcceptBfrd,     ///< BFRD 0→1: m_activeSector loaded into data FIFO (accepted).
        AcceptBiosAuto, ///< BIOS-owned CdAsyncReadSector path (enableDataRead()) loaded the FIFO.
        DrqstsOn,       ///< DRQSTS asserted (BFRD=1 and data FIFO non-empty).
        CpuRddatRead,  ///< First readData() byte consumed from the current accepted-sector phase.
        Dma3Read,      ///< First readDma() word consumed from the current accepted-sector phase.
        DrainComplete,  ///< Data FIFO exhausted; DRQSTS would fall.
        // XA sector classification events (PR-RV23)
        XaAudioDeliver,  ///< XA-ADPCM sector decoded to SPU; INT1 suppressed.
        CpuDataDeliver,  ///< Sector routed to CPU data FIFO; INT1 will fire.
        FilterReject,    ///< XA-ADPCM sector rejected by file/channel filter; scan continues.
        FormatReject,    ///< Raw Mode2 sector with invalid subheader in XA mode; CPU data path.
        SubmodeReject,   ///< Mode2/Form2 sector without Audio+Realtime bits; CPU data path.
    };

    /// One entry in the rolling sector-phase transition history.
    struct PhaseTraceEntry
    {
        u32 lba = 0;
        SectorPhaseReason reason = SectorPhaseReason::QueuePromote;
    };

    static constexpr size_t PHASE_TRACE_CAPACITY = 32u;

    /// Number of valid entries in the rolling phase-trace ring (≤ PHASE_TRACE_CAPACITY).
    size_t phaseTraceCount() const;

    /// Retrieve an entry by logical index where 0 is the oldest retained entry.
    PhaseTraceEntry phaseTraceEntry(size_t index) const;

    /// Compact multi-line textual summary of the last @p last transitions.
    /// Suitable for end-of-run diagnostics output.
    std::string formatPhaseTraceSummary(size_t last = 10) const;

    /// End-of-run XA sector classification summary: per-category counts, INT1
    /// suppression totals, and current mode/filter state.  Suitable for
    /// confirming how sectors were routed after Setmode 0xE0 + ReadS/ReadN.
    std::string formatXaClassificationSummary() const;

    /// ADPBUSY lifecycle summary: when ADPBUSY rose/fell (LBA) and sector count
    /// consumed while busy.  Answers "did XA playback actually run, and when?".
    std::string formatAdpbusyLifecycleSummary() const;

    /// Post-ReadS/ReadN XA stream summary: counts and flags from the moment
    /// the most recent XA-enabled ReadS/ReadN was issued.  Avoids cumulative
    /// noise from pre-stream initialization reads.
    std::string formatPostStreamSummary() const;

    /// Compact per-sector breakdown of the first 32 CPU-visible sectors
    /// delivered after the most recent XA-enabled ReadS/ReadN: LBA, payload
    /// size and mode, XA subheader (if available), first/last 16 bytes, and
    /// whether DMA3 drained the sector.  Answers "what exact bytes does the
    /// game see for each non-ADPCM stream sector?" (PR-RV28).
    std::string formatCpuPayloadSummary() const;

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

    // ---- Sector phase trace ------------------------------------------------
    std::array<PhaseTraceEntry, PHASE_TRACE_CAPACITY> m_phaseRing{};
    size_t m_phaseRingHead = 0;   ///< Next write slot.
    size_t m_phaseRingCount = 0;  ///< Valid entries (≤ PHASE_TRACE_CAPACITY).
    u32 m_activeLba = 0;          ///< LBA of the sector currently held in m_activeSector.
    std::deque<u32> m_bufferedReadLbas; ///< Parallel to m_bufferedReadSectors.
    bool m_phaseFirstCpuReadFired = false; ///< Reset on AcceptBfrd; fires CpuRddatRead once.
    bool m_phaseFirstDmaFired = false;     ///< Reset on AcceptBfrd; fires Dma3Read once.
    bool m_phaseDrainFired = false;        ///< Reset on AcceptBfrd; fires DrainComplete once.

    // ---- XA sector classification counters (PR-RV23) -----------------------
    u32 m_xaDeliveryCount = 0;    ///< Sectors decoded to SPU (INT1 suppressed).
    u32 m_filterRejectCount = 0;  ///< Sectors rejected by XA file/channel filter.
    u32 m_formatRejectCount = 0;  ///< Mode2 sectors with invalid subheader in XA mode.
    u32 m_submodeRejectCount = 0; ///< Mode2/Form2 non-ADPCM sectors in XA mode.
    u32 m_cpuDeliveryCount = 0;   ///< Sectors routed to CPU data path (INT1 fired).
    u8 m_xaLastCodingInfo = 0;    ///< codingInfo byte of the most recently consumed XA-ADPCM sector.

    // ---- ADPBUSY + post-stream validator (PR-RV26/27/30) -------------------
    bool m_xaPlaybackBusy = false;   ///< ADPBUSY: disc-XA decoder active (bit 2 of HSTS).
    u32 m_xaPlaybackBusyRoseLba = 0; ///< LBA where ADPBUSY rose; 0 if never set.
    u32 m_xaPlaybackBusyFellLba = 0; ///< LBA where ADPBUSY last fell; 0 if still busy or never.
    u32 m_xaSectorsWhileBusy = 0;    ///< XA-ADPCM sectors consumed since ADPBUSY rose.
    u32 m_streamStartXaCount = 0;    ///< m_xaDeliveryCount snapshot at last XA-enabled ReadS/ReadN.
    u32 m_streamStartCpuCount = 0;   ///< m_cpuDeliveryCount snapshot at last XA-enabled ReadS/ReadN.
    bool m_streamStarted = false;    ///< True once an XA-enabled ReadS/ReadN was issued.

    // ---- Post-stream CPU payload tracer (PR-RV28) --------------------------
    /// Per-sector snapshot recorded at INT1 publication for each CPU-visible
    /// sector after an XA-enabled ReadS/ReadN.
    struct CpuSectorRecord
    {
        u32 lba = 0;
        size_t fifoBytes = 0;      ///< Bytes loaded into the data FIFO (= sector payload size).
        bool dma3Started = false;  ///< First readDma() byte consumed from this sector.
        bool drained = false;      ///< Data FIFO fully exhausted (DrainComplete).
        bool finalized = false;    ///< dma3Started/drained have been written by next-sector INT1.
        u8 payloadMode = 0;        ///< 0=user2048, 1=form2-2324, 2=raw2340, 3=other.
        bool hasXaSub = false;     ///< XA subheader decoded (only possible in raw2340 mode).
        u8 xaFile = 0;
        u8 xaChannel = 0;
        u8 xaSubmode = 0;
        u8 xaCoding = 0;
        std::array<u8, 16> firstBytes{};
        std::array<u8, 16> lastBytes{};
        size_t cpuFirstOffset = ~size_t{0}; ///< FIFO byte offset at first CPU readData(); ~0 if no CPU read.
        size_t dmaFirstOffset = ~size_t{0}; ///< FIFO byte offset at first DMA readDma(); ~0 if no DMA read.
        size_t cpuBytesRead = 0;            ///< Total bytes consumed via readData().
        size_t dmaBytesRead = 0;            ///< Total bytes consumed via readDma().
        size_t finalOffset = 0;             ///< Total bytes consumed (CPU+DMA) before next INT1.
    };

    static constexpr size_t CPU_RECORD_CAPACITY = 32u;
    std::array<CpuSectorRecord, CPU_RECORD_CAPACITY> m_cpuRecords{};
    size_t m_cpuRecordCount = 0;
    size_t m_dataFifoConsumedBytes = 0; ///< Bytes consumed from current accepted sector (CPU+DMA).

    void recordPhaseTrace(u32 lba, SectorPhaseReason reason);

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
    void updateDataPadForActiveSector();
    bool queueReadSector();
    bool loadReadSector(std::vector<u8>& outSector);
    u32 currentReadCycles() const;
    void snapshotCpuSector(u32 lba, const std::vector<u8>& sector);
};

} // namespace runtime
} // namespace psxrecomp
