#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <cstdint>
#include <string>

namespace psxrecomp
{
namespace runtime
{

/// Categories for the root cause of an execution stall.
enum class StallReason : u8
{
    Unknown,
    CdromIrqWait,
    CdromPolling,
    ControllerPolling,
    MdecPolling,
    BiosEventWait,
    BiosFileIo,
    IrqDelivery,
    DmaWait,
    GpuBusy,
    SpinLoop,
    StepBudget
};

/// Returns a human-readable label for a stall reason.
const char* stallReasonLabel(StallReason reason);

/// Single MMIO access record stored in the ring buffer.
struct MmioAccessEntry
{
    Address address = 0;
    u32 value = 0;
    bool isWrite = false;
};

/// Single BIOS call record stored in the ring buffer.
struct BiosCallEntry
{
    u32 vector = 0; ///< 0xA0, 0xB0, or 0xC0
    u32 functionId = 0;
    u32 arg0 = 0;
};

/// Single DMA trigger record stored in the ring buffer.
struct DmaTriggerEntry
{
    u8 port = 0;
    u32 baseAddress = 0;
    u32 blockControl = 0;
};

/// Compact snapshot of CD-ROM + IRQ state.
struct CdromIrqSnapshot
{
    u8 cdromIrqFlags = 0;
    u16 irqStatus = 0;
    u16 irqMask = 0;
    bool cdromHasIrq = false;
};

/// Fixed-size ring buffer for telemetry entries.
template <typename T, size_t N> class RingBuffer
{
  public:
    static constexpr size_t Capacity = N;

    void push(const T& entry)
    {
        m_data[m_writePos % N] = entry;
        ++m_writePos;
        if (m_count < N)
        {
            ++m_count;
        }
    }

    /// Number of valid entries.
    size_t count() const
    {
        return m_count;
    }

    /// Access the i-th most recent entry (0 = newest).
    const T& recent(size_t i) const
    {
        return m_data[(m_writePos - 1 - i) % N];
    }

    /// Access underlying data for iteration (oldest→newest when walked
    /// from startIndex).
    const T& at(size_t index) const
    {
        return m_data[index % N];
    }

    /// Index of the oldest valid entry in the underlying array.
    size_t oldestIndex() const
    {
        if (m_count < N)
        {
            return 0;
        }
        return m_writePos % N;
    }

    void clear()
    {
        m_writePos = 0;
        m_count = 0;
    }

  private:
    std::array<T, N> m_data{};
    size_t m_writePos = 0;
    size_t m_count = 0;
};

/**
 * @brief Observes execution telemetry and classifies stall reasons.
 *
 * Maintains small ring buffers of recent PCs, MMIO accesses, BIOS
 * calls, DMA triggers, and CD-ROM/IRQ snapshots.  When the step
 * budget fires (or a watchdog timeout occurs), produces a compact
 * summary of the most likely stall cause.
 */
class StallClassifier
{
  public:
    static constexpr size_t PC_RING_SIZE = 64;
    static constexpr size_t MMIO_RING_SIZE = 32;
    static constexpr size_t BIOS_RING_SIZE = 16;
    static constexpr size_t DMA_RING_SIZE = 8;
    static constexpr size_t CDROM_RING_SIZE = 8;

    /// Record a PC observation (call from setProgramCounter).
    void recordPc(Address pc);

    /// Record an MMIO read or write.
    void recordMmioAccess(Address address, u32 value, bool isWrite);

    /// Record a BIOS vector call (A0/B0/C0).
    void recordBiosCall(u32 vector, u32 functionId, u32 arg0);

    /// Record a DMA transfer trigger.
    void recordDmaTrigger(u8 port, u32 baseAddress, u32 blockControl);

    /// Record a CD-ROM / IRQ state snapshot.
    void recordCdromIrqState(u8 cdromIrqFlags, u16 irqStatus, u16 irqMask, bool cdromHasIrq);

    /// Classify the current stall and return a compact summary string.
    std::string classify() const;

    /// Access the PC ring buffer (for testing / advanced queries).
    const RingBuffer<Address, PC_RING_SIZE>& pcRing() const;

    /// Access the MMIO ring buffer.
    const RingBuffer<MmioAccessEntry, MMIO_RING_SIZE>& mmioRing() const;

    /// Access the BIOS call ring buffer.
    const RingBuffer<BiosCallEntry, BIOS_RING_SIZE>& biosRing() const;

    /// Access the DMA ring buffer.
    const RingBuffer<DmaTriggerEntry, DMA_RING_SIZE>& dmaRing() const;

    /// Access the CD-ROM/IRQ ring buffer.
    const RingBuffer<CdromIrqSnapshot, CDROM_RING_SIZE>& cdromRing() const;

    /// Reset all ring buffers.
    void reset();

  private:
    /// Detect repeated-PC patterns (spin loops).
    StallReason detectPcLoop() const;

    /// Detect repeated MMIO access patterns.
    StallReason detectMmioLoop() const;

    /// Detect repeated BIOS call patterns.
    StallReason detectBiosLoop() const;

    /// Classify MMIO address into a subsystem category.
    static StallReason classifyMmioAddress(Address address);

    /// Format the ring buffers into a human-readable summary.
    std::string formatSummary(StallReason reason) const;

    RingBuffer<Address, PC_RING_SIZE> m_pcRing;
    RingBuffer<MmioAccessEntry, MMIO_RING_SIZE> m_mmioRing;
    RingBuffer<BiosCallEntry, BIOS_RING_SIZE> m_biosRing;
    RingBuffer<DmaTriggerEntry, DMA_RING_SIZE> m_dmaRing;
    RingBuffer<CdromIrqSnapshot, CDROM_RING_SIZE> m_cdromRing;
};

} // namespace runtime
} // namespace psxrecomp
