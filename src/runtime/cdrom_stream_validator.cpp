// PR-RV27: Post-ReadS XA stream summary implementation.
// PR-RV30: ADPBUSY lifecycle summary.
//
// Reports XA/CPU sector counts scoped to the most recent XA-enabled ReadS/ReadN
// command, avoiding cumulative noise from pre-stream initialisation reads.

#include "psxrecomp/runtime/cdrom.h"

#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

std::string Cdrom::formatPostStreamSummary() const
{
    if (!m_streamStarted)
    {
        return "(no XA-enabled ReadS/ReadN issued)\n";
    }
    std::ostringstream os;
    const u32 xaAfter = m_xaDeliveryCount - m_streamStartXaCount;
    const u32 cpuAfter = m_cpuDeliveryCount - m_streamStartCpuCount;
    os << "=== Post-ReadS XA Stream Summary ===\n";
    os << "  xa_consumed_after_stream:    " << xaAfter << "\n";
    os << "  cpu_data_after_stream:       " << cpuAfter << "\n";
    os << "  adpbusy_became_1:            " << (xaAfter > 0u ? "yes" : "no") << "\n";
    os << "  dma3_after_stream:           " << (cpuAfter > 0u ? "yes (inferred)" : "no") << "\n";
    os << "  mdec1_after_stream:          unknown\n";
    return os.str();
}

std::string Cdrom::formatAdpbusyLifecycleSummary() const
{
    std::ostringstream os;
    os << "=== CDROM ADPBUSY Lifecycle Summary ===\n";
    os << "  adpbusy_now:      " << (m_xaPlaybackBusy ? "set" : "clear") << "\n";
    if (m_xaSectorsWhileBusy > 0)
    {
        os << "  rose_at_lba:      0x" << std::hex << std::setw(6) << std::setfill('0')
           << m_xaPlaybackBusyRoseLba << std::dec << std::setfill(' ') << "\n";
        if (m_xaPlaybackBusy)
        {
            os << "  fell_at_lba:      (still busy)\n";
        }
        else
        {
            os << "  fell_at_lba:      0x" << std::hex << std::setw(6) << std::setfill('0')
               << m_xaPlaybackBusyFellLba << std::dec << std::setfill(' ') << "\n";
        }
        os << "  sectors_busy:     " << m_xaSectorsWhileBusy << "\n";
    }
    else
    {
        os << "  (ADPBUSY never rose)\n";
    }
    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
