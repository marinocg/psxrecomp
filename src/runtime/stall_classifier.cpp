#include "psxrecomp/runtime/stall_classifier.h"
#include "psxrecomp/runtime/memory_map.h"
#include "stall_heap_debug.h"
#include "stall_write_watch.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace psxrecomp
{
namespace runtime
{

// ---------------------------------------------------------------------------
// StallReason labels
// ---------------------------------------------------------------------------

const char* stallReasonLabel(StallReason reason)
{
    switch (reason)
    {
    case StallReason::Unknown:
        return "unknown";
    case StallReason::CdromIrqWait:
        return "CD-ROM IRQ wait";
    case StallReason::CdromPolling:
        return "CD-ROM register polling";
    case StallReason::ControllerPolling:
        return "polling JOY_STAT/JOY_CTRL forever";
    case StallReason::MdecPolling:
        return "polling MDEC / waiting for decode output";
    case StallReason::BiosEventWait:
        return "stalled in WaitEvent / TestEvent";
    case StallReason::BiosFileIo:
        return "stalled in BIOS file/device I/O";
    case StallReason::IrqDelivery:
        return "waiting for IRQ delivery";
    case StallReason::DmaWait:
        return "waiting for DMA completion";
    case StallReason::GpuBusy:
        return "polling GPU busy flag";
    case StallReason::SpinLoop:
        return "CPU spin loop (repeated PC)";
    case StallReason::StepBudget:
        return "step budget exhausted";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// StallClassifier — record helpers
// ---------------------------------------------------------------------------

StallClassifier::StallClassifier() : m_watchedWriteRanges(detail::parseWatchedWriteRangesFromEnv())
{
}

void StallClassifier::recordPc(Address pc)
{
    m_pcRing.push(pc);
}

void StallClassifier::recordMmioAccess(Address address, u32 value, bool isWrite)
{
    m_mmioRing.push({address, value, isWrite});
}

void StallClassifier::recordBiosCall(u32 vector, u32 functionId, u32 arg0)
{
    m_biosRing.push({vector, functionId, arg0});
}

void StallClassifier::recordDmaTrigger(u8 port, u32 baseAddress, u32 blockControl)
{
    m_dmaRing.push({port, baseAddress, blockControl});
}

void StallClassifier::recordCdromIrqState(u8 cdromIrqFlags, u16 irqStatus, u16 irqMask,
                                          bool cdromHasIrq)
{
    m_cdromRing.push({cdromIrqFlags, irqStatus, irqMask, cdromHasIrq});
}

bool StallClassifier::shouldWatchRamWrite(Address address, u8 size) const
{
    return detail::overlapsWatchedWrite(m_watchedWriteRanges, address, size);
}

void StallClassifier::recordRamWrite(Address writerPc, Address address, u8 size, u32 oldValue,
                                     u32 newValue)
{
    if (!shouldWatchRamWrite(address, size))
    {
        return;
    }

    m_watchedWriteRing.push({writerPc, address & 0x1FFFFFFFu, size, oldValue, newValue});
}

void StallClassifier::recordRamCopyProvenance(std::string source, std::string detail,
                                              Address writerPc, Address destination,
                                              u32 actualLength, u32 requestedLength,
                                              bool destinationInRam, bool destinationOverflow,
                                              bool shortRead)
{
    m_copyProvenanceRing.push({std::move(source), std::move(detail), writerPc, destination,
                               actualLength, requestedLength, destinationInRam, destinationOverflow,
                               shortRead});
}

bool StallClassifier::isWatchingRamWrites() const
{
    return !m_watchedWriteRanges.empty();
}

void StallClassifier::attachSystem(const PsxSystem* system)
{
    m_system = system;
}

void StallClassifier::reset()
{
    m_pcRing.clear();
    m_mmioRing.clear();
    m_biosRing.clear();
    m_dmaRing.clear();
    m_cdromRing.clear();
    m_watchedWriteRing.clear();
    m_copyProvenanceRing.clear();
}

// ---------------------------------------------------------------------------
// Ring buffer accessors
// ---------------------------------------------------------------------------

const RingBuffer<Address, StallClassifier::PC_RING_SIZE>& StallClassifier::pcRing() const
{
    return m_pcRing;
}

const RingBuffer<MmioAccessEntry, StallClassifier::MMIO_RING_SIZE>&
StallClassifier::mmioRing() const
{
    return m_mmioRing;
}

const RingBuffer<BiosCallEntry, StallClassifier::BIOS_RING_SIZE>& StallClassifier::biosRing() const
{
    return m_biosRing;
}

const RingBuffer<DmaTriggerEntry, StallClassifier::DMA_RING_SIZE>& StallClassifier::dmaRing() const
{
    return m_dmaRing;
}

const RingBuffer<CdromIrqSnapshot, StallClassifier::CDROM_RING_SIZE>&
StallClassifier::cdromRing() const
{
    return m_cdromRing;
}

const RingBuffer<WatchedRamWriteEntry, StallClassifier::WATCHED_WRITE_RING_SIZE>&
StallClassifier::watchedWriteRing() const
{
    return m_watchedWriteRing;
}

const RingBuffer<RamCopyProvenanceEntry, StallClassifier::COPY_PROVENANCE_RING_SIZE>&
StallClassifier::copyProvenanceRing() const
{
    return m_copyProvenanceRing;
}

// ---------------------------------------------------------------------------
// Classification logic
// ---------------------------------------------------------------------------

StallReason StallClassifier::detectPcLoop() const
{
    if (m_pcRing.count() < 4)
    {
        return StallReason::Unknown;
    }

    // Check for a short repeated-PC cycle (period 1..8).
    for (size_t period = 1; period <= 8 && period * 3 <= m_pcRing.count(); ++period)
    {
        bool match = true;
        for (size_t i = 0; i < period * 2 && match; ++i)
        {
            if (m_pcRing.recent(i) != m_pcRing.recent(i + period))
            {
                match = false;
            }
        }
        if (match)
        {
            return StallReason::SpinLoop;
        }
    }
    return StallReason::Unknown;
}

StallReason StallClassifier::detectMmioLoop() const
{
    if (m_mmioRing.count() < 4)
    {
        return StallReason::Unknown;
    }

    // Count how many of the last N MMIO accesses hit the same address.
    const size_t window = std::min<size_t>(m_mmioRing.count(), 16);
    std::unordered_map<Address, size_t> freq;
    for (size_t i = 0; i < window; ++i)
    {
        ++freq[m_mmioRing.recent(i).address];
    }

    // If >= 75% of recent accesses are to one address, classify it.
    for (auto& [addr, cnt] : freq)
    {
        if (cnt * 4 >= window * 3)
        {
            return classifyMmioAddress(addr);
        }
    }
    return StallReason::Unknown;
}

StallReason StallClassifier::detectBiosLoop() const
{
    if (m_biosRing.count() < 3)
    {
        return StallReason::Unknown;
    }

    // Check if the last several BIOS calls are the same function.
    const size_t window = std::min<size_t>(m_biosRing.count(), 8);
    const auto& latest = m_biosRing.recent(0);
    size_t sameCount = 0;
    for (size_t i = 0; i < window; ++i)
    {
        const auto& e = m_biosRing.recent(i);
        if (e.vector == latest.vector && e.functionId == latest.functionId)
        {
            ++sameCount;
        }
    }

    if (sameCount * 4 < window * 3)
    {
        return StallReason::Unknown;
    }

    // Classify known BIOS functions.
    if (latest.vector == 0xB0)
    {
        // B0:07 = DeliverEvent, B0:08 = OpenEvent, B0:09 = CloseEvent
        // B0:0A = WaitEvent, B0:0B = TestEvent
        if (latest.functionId == 0x0A || latest.functionId == 0x0B)
        {
            return StallReason::BiosEventWait;
        }
        // B0:32 = open, B0:33 = lseek, B0:34 = read, B0:35 = write
        if (latest.functionId >= 0x32 && latest.functionId <= 0x35)
        {
            return StallReason::BiosFileIo;
        }
    }
    if (latest.vector == 0xA0)
    {
        // A0:54/A0:56 are CdInit/CdRemove, and A0:71/A0:72 are the
        // related _96_init/_96_remove BIOS CD setup helpers.
        if (latest.functionId == 0x54 || latest.functionId == 0x56 || latest.functionId == 0x71 ||
            latest.functionId == 0x72)
        {
            return StallReason::CdromIrqWait;
        }
    }

    return StallReason::Unknown;
}

StallReason StallClassifier::classifyMmioAddress(Address address)
{
    // CD-ROM registers: 0x1F801800..0x1F801803
    if (address >= Mmio::CDROM_BASE && address < Mmio::CDROM_BASE + Mmio::CDROM_SIZE)
    {
        return StallReason::CdromPolling;
    }
    // Controller/SIO0: 0x1F801040..0x1F80104F
    if (address >= Mmio::CONTROLLER_BASE && address < Mmio::CONTROLLER_BASE + Mmio::CONTROLLER_SIZE)
    {
        return StallReason::ControllerPolling;
    }
    // MDEC: 0x1F801820..0x1F801828
    if (address >= Mmio::MDEC_BASE && address < Mmio::MDEC_BASE + Mmio::MDEC_SIZE)
    {
        return StallReason::MdecPolling;
    }
    // GPU: 0x1F801810..0x1F801814
    if (address == Mmio::GPU_GP0 || address == Mmio::GPU_GP1)
    {
        return StallReason::GpuBusy;
    }
    // DMA
    if (address >= Mmio::DMA_BASE && address < Mmio::DMA_BASE + Mmio::DMA_SIZE)
    {
        return StallReason::DmaWait;
    }
    // IRQ status/mask
    if (address == Mmio::INTERRUPT_STATUS || address == Mmio::INTERRUPT_MASK)
    {
        return StallReason::IrqDelivery;
    }
    return StallReason::Unknown;
}

std::string StallClassifier::formatAllocatorHeapDump() const
{
    return m_system == nullptr ? std::string{} : detail::formatAllocatorHeapDump(*m_system);
}

bool StallClassifier::shouldDumpAllocatorHeap() const
{
    return detail::shouldDumpAllocatorHeap(m_system, m_pcRing);
}

std::string StallClassifier::formatWatchedRamWrites() const
{
    return detail::formatWatchedRamWrites(m_watchedWriteRing);
}

std::string StallClassifier::formatRamCopyProvenance() const
{
    std::ostringstream os;
    os << "Recent RAM copy provenance (newest first):\n";
    if (m_copyProvenanceRing.count() == 0)
    {
        os << "  none\n";
        return os.str();
    }

    const size_t count = std::min<size_t>(m_copyProvenanceRing.count(), 12);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& entry = m_copyProvenanceRing.recent(i);
        os << "  source=" << entry.source << " pc=0x" << std::hex << entry.writerPc << " dst=0x"
           << entry.destination << " len=" << std::dec << entry.actualLength;
        if (entry.requestedLength != entry.actualLength)
        {
            os << "/" << entry.requestedLength;
        }
        if (!entry.destinationInRam)
        {
            os << " out-of-ram";
        }
        if (entry.destinationOverflow)
        {
            os << " overflow";
        }
        if (entry.shortRead)
        {
            os << " short-read";
        }
        if (!entry.detail.empty())
        {
            os << " " << entry.detail;
        }
        os << "\n";
    }
    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
