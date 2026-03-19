#include "psxrecomp/runtime/cdrom.h"

#include "cdrom_shared.h"

#include <array>
#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
const char* irqLabel(u8 irqType)
{
    switch (irqType)
    {
    case 1:
        return "INT1";
    case 2:
        return "INT2";
    case 3:
        return "INT3";
    case 4:
        return "INT4";
    case 5:
        return "INT5";
    default:
        return "INT?";
    }
}
} // namespace

void Cdrom::noteIrqCallbackDispatch(u8 irqType, bool callbacksDispatched)
{
    if (!callbacksDispatched || irqType < 1u || irqType > IRQ_LIFECYCLE_TYPE_COUNT)
    {
        return;
    }

    const IrqLifecycleRecord& record = m_irqLifecycle[irqType - 1u];
    if (record.publishGeneration == 0)
    {
        return;
    }

    if (m_lastCallbackDispatchType == irqType &&
        m_lastCallbackDispatchGeneration == record.publishGeneration)
    {
        ++m_irqLifecycle[irqType - 1u].callbackRedispatchCount;
    }

    m_lastCallbackDispatchType = irqType;
    m_lastCallbackDispatchGeneration = record.publishGeneration;
}

u32 Cdrom::irqPublishGeneration(u8 irqType) const
{
    if (irqType < 1u || irqType > IRQ_LIFECYCLE_TYPE_COUNT)
    {
        return 0;
    }
    return m_irqLifecycle[irqType - 1u].publishGeneration;
}

std::string Cdrom::formatIrqLifecycleSummary() const
{
    std::ostringstream os;
    os << "=== CDROM IRQ Lifecycle Summary ===\n";
    for (u8 irqType = 1; irqType <= IRQ_LIFECYCLE_TYPE_COUNT; ++irqType)
    {
        const IrqLifecycleRecord& record = m_irqLifecycle[irqType - 1u];
        os << "  " << irqLabel(irqType) << " publish_gen=" << std::setw(3)
           << record.publishGeneration << " queued=" << std::setw(3) << record.queuedCount
           << " published=" << std::setw(3) << record.publishedCount << " acked=" << std::setw(3)
           << record.ackedCount << " deasserted=" << std::setw(3) << record.deassertedCount
           << " callback_redispatch_no_new_publish=" << record.callbackRedispatchCount << "\n";
    }

    const IrqLifecycleRecord& int4 = m_irqLifecycle[cdrom_detail::INT4 - 1u];
    os << "  INT4 published: " << int4.publishedCount << "\n";
    os << "  INT4 acked: " << int4.ackedCount << "\n";
    os << "  INT4 redispatched without new publish: " << int4.callbackRedispatchCount << "\n";
    os << "  INT4 HCLRCTL cleared active: " << (m_int4HclrctlClearCount != 0u ? "yes" : "no")
       << "\n";
    os << "  top-level CD IRQ deassert after INT4 ack: "
       << (m_int4TopLevelDeassertAfterAck ? "yes" : "no") << "\n";
    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
