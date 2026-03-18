// PR-RV27: Post-ReadS XA stream summary implementation.
//
// Reports XA/CPU sector counts scoped to the most recent XA-enabled ReadS/ReadN
// command, avoiding cumulative noise from pre-stream initialisation reads.

#include "psxrecomp/runtime/cdrom.h"

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

} // namespace runtime
} // namespace psxrecomp
