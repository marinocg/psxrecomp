#include "psxrecomp/runtime/stall_classifier.h"
#include "psxrecomp/runtime/control_plane_timeline.h"
#include "psxrecomp/runtime/psx_system.h"
#include "stall_heap_debug.h"
#include "stall_write_watch.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

// ---------------------------------------------------------------------------
// Top-level classify()
// ---------------------------------------------------------------------------

std::string StallClassifier::classify() const
{
    StallReason reason = detectBiosLoop();
    if (reason == StallReason::Unknown)
    {
        reason = detectMmioLoop();
    }
    if (reason == StallReason::Unknown)
    {
        reason = detectPcLoop();
    }
    if (reason == StallReason::Unknown && m_pcRing.count() > 0)
    {
        reason = StallReason::StepBudget;
    }
    return formatSummary(reason);
}

std::string StallClassifier::formatRecentMemoryActivity() const
{
    std::ostringstream os;
    if (isWatchingRamWrites())
    {
        os << formatWatchedRamWrites();
    }
    os << formatRamCopyProvenance();
    return os.str();
}

// ---------------------------------------------------------------------------
// Summary formatter
// ---------------------------------------------------------------------------

std::string StallClassifier::formatSummary(StallReason reason) const
{
    std::ostringstream os;
    os << "=== Stall Reason Summary ===\n";
    os << "Diagnosis: " << stallReasonLabel(reason) << "\n";

    if (m_pcRing.count() > 0)
    {
        const size_t n = std::min<size_t>(m_pcRing.count(), 8);
        os << "Last PCs (newest first):";
        for (size_t i = 0; i < n; ++i)
        {
            os << " 0x" << std::hex << m_pcRing.recent(i);
        }
        os << "\n";
    }

    if (m_mmioRing.count() > 0)
    {
        const size_t n = std::min<size_t>(m_mmioRing.count(), 6);
        os << "Last MMIO accesses (newest first):\n";
        for (size_t i = 0; i < n; ++i)
        {
            const auto& e = m_mmioRing.recent(i);
            os << "  " << (e.isWrite ? "W" : "R") << " 0x" << std::hex << e.address << " = 0x"
               << e.value << "\n";
        }
    }

    if (m_biosRing.count() > 0)
    {
        const size_t n = std::min<size_t>(m_biosRing.count(), 4);
        os << "Last BIOS calls (newest first):\n";
        for (size_t i = 0; i < n; ++i)
        {
            const auto& e = m_biosRing.recent(i);
            os << "  vector=0x" << std::hex << e.vector << " func=0x" << e.functionId
               << " a0=0x" << e.arg0 << "\n";
        }
    }

    if (m_dmaRing.count() > 0)
    {
        const size_t n = std::min<size_t>(m_dmaRing.count(), 4);
        os << "Last DMA triggers (newest first):\n";
        for (size_t i = 0; i < n; ++i)
        {
            const auto& e = m_dmaRing.recent(i);
            os << "  port=" << static_cast<int>(e.port) << " base=0x" << std::hex
               << e.baseAddress << " block=0x" << e.blockControl << "\n";
        }
    }

    if (m_cdromRing.count() > 0)
    {
        const size_t n = std::min<size_t>(m_cdromRing.count(), 4);
        os << "Last CD-ROM/IRQ snapshots (newest first):\n";
        for (size_t i = 0; i < n; ++i)
        {
            const auto& e = m_cdromRing.recent(i);
            os << "  cdIrqFlags=0x" << std::hex << static_cast<int>(e.cdromIrqFlags)
               << " irqStat=0x" << e.irqStatus << " irqMask=0x" << e.irqMask
               << " cdHasIrq=" << (e.cdromHasIrq ? "yes" : "no") << "\n";
        }
    }

    // Recent control-plane events: compact post-mortem of the IRQ/DMA pipeline.
    if (m_system != nullptr)
    {
        const RuntimeControlPlaneTimeline& tl = m_system->cpTimeline();
        if (tl.count() > 0)
        {
            const size_t n = std::min<size_t>(tl.count(), 8u);
            os << "Recent control-plane events (newest first):\n";
            for (size_t i = 0; i < n; ++i)
            {
                const CpEvent& ev = tl.at(tl.count() - 1u - i);
                os << "  [" << std::dec << i << "] " << cpEventKindLabel(ev.kind)
                   << " aux=" << static_cast<unsigned>(ev.aux)
                   << " val=0x" << std::hex << ev.value
                   << " before=0x" << ev.before
                   << " after=0x" << ev.after << "\n";
            }
        }
    }

    if (shouldDumpAllocatorHeap())
    {
        os << formatAllocatorHeapDump();
    }
    if (isWatchingRamWrites())
    {
        os << formatWatchedRamWrites();
    }
    os << formatRamCopyProvenance();

    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
