#include "psxrecomp/runtime/psx_system.h"

#include "irq_trace_utils.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

bool PsxSystem::dispatchIrqChains()
{
    // Skip delivery inside critical sections.
    if (m_criticalSectionDepth > 0)
    {
        return false;
    }
    if (!m_callbackInvoker)
    {
        return false;
    }

    constexpr int kMaxNodesPerChain = 64;
    for (u32 prio = 0; prio < m_irqChainHeads.size(); ++prio)
    {
        u32 node = m_irqChainHeads[prio];
        for (int safety = 0; node != 0 && safety < kMaxNodesPerChain; ++safety)
        {
            const u32 func2 = read<u32>(node + 0x04);
            const u32 func1 = read<u32>(node + 0x08);
            if (traceIrqFlowEnabled())
            {
                std::ostringstream msg;
                msg << "event=irq_dispatch_order source=irq_chain prio=" << std::dec << prio
                    << " index=" << safety << " node=0x" << std::hex << node << " func1=0x" << func1
                    << " func2=0x" << func2;
                m_logger.log(LogLevel::Info, "irq_trace", msg.str());
            }

            if (func1 != 0)
            {
                u32 func1Result = 0;
                try
                {
                    func1Result = invokeCallbackRaw(func1);
                }
                catch (const ReturnFromExceptionSignal&)
                {
                    if (traceIrqFlowEnabled())
                    {
                        m_logger.log(LogLevel::Info, "irq_trace",
                                     "event=return_from_exception source=irq_chain_func1");
                    }
                    return true;
                }

                if (func1Result != 0 && func2 != 0)
                {
                    try
                    {
                        (void)invokeCallbackRaw(func2);
                    }
                    catch (const ReturnFromExceptionSignal&)
                    {
                        if (traceIrqFlowEnabled())
                        {
                            m_logger.log(LogLevel::Info, "irq_trace",
                                         "event=return_from_exception source=irq_chain_func2");
                        }
                        return true;
                    }
                }
            }

            node = read<u32>(node + 0x00);
        }
    }
    return false;
}

u32 PsxSystem::criticalSectionDepth() const
{
    return m_criticalSectionDepth;
}

} // namespace runtime
} // namespace psxrecomp
