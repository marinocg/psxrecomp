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
    const bool canInvokeUserChains = static_cast<bool>(m_callbackInvoker);

    // Restore any MMIO pointers that a buffer overrun may have zeroed.
    for (u32 prio = 0; prio < m_irqChainHeads.size(); ++prio)
    {
        if (m_irqChainSnapshots[prio].baseAddress != 0)
        {
            restoreIrqChainSnapshot(prio);
        }
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

            if (!canInvokeUserChains)
            {
                break;
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

        // PSX-SPX: the default priority-0 chain includes the BIOS CD-ROM IRQ
        // handlers ahead of lower-priority chains. Model the BIOS-owned CD IRQ
        // service path as the built-in tail of prio 0 so user-installed prio 2
        // nodes (such as libcd-style handlers) observe the same ordering.
        if (prio == 0)
        {
            try
            {
                (void)serviceBiosCdromInterrupt();
            }
            catch (const ReturnFromExceptionSignal&)
            {
                if (traceIrqFlowEnabled())
                {
                    m_logger.log(LogLevel::Info, "irq_trace",
                                 "event=return_from_exception source=irq_chain_bios_cdrom");
                }
                return true;
            }
        }
    }
    return false;
}

u32 PsxSystem::criticalSectionDepth() const
{
    return m_criticalSectionDepth;
}

void PsxSystem::saveIrqChainSnapshot(u32 priority, u32 structAddress)
{
    if (priority >= m_irqChainSnapshots.size())
    {
        return;
    }
    auto& snap = m_irqChainSnapshots[priority];
    snap.baseAddress = structAddress;
    for (u32 i = 0; i < IRQ_CHAIN_DATA_SNAPSHOT_WORDS; ++i)
    {
        const u32 addr = structAddress + i * sizeof(u32);
        if (isMainRamAddress(normalizeAddress(addr), sizeof(u32)))
        {
            snap.words[i] = read<u32>(addr);
        }
        else
        {
            snap.words[i] = 0;
        }
    }
}

void PsxSystem::restoreIrqChainSnapshot(u32 priority)
{
    if (priority >= m_irqChainSnapshots.size())
    {
        return;
    }
    const auto& snap = m_irqChainSnapshots[priority];
    if (snap.baseAddress == 0)
    {
        return;
    }
    // Skip +0 (next ptr managed by SysEnqIntRP) and +4/+8 (func ptrs).
    // Restore words at offsets +12..+60 only when the current value is
    // zero and the snapshot held a plausible MMIO pointer (0x1F80xxxx).
    constexpr u32 kMmioBase = 0x1F800000u;
    constexpr u32 kMmioEnd = 0x1F900000u;
    for (u32 i = 3; i < IRQ_CHAIN_DATA_SNAPSHOT_WORDS; ++i)
    {
        const u32 saved = snap.words[i];
        if (saved >= kMmioBase && saved < kMmioEnd)
        {
            const u32 addr = snap.baseAddress + i * sizeof(u32);
            if (isMainRamAddress(normalizeAddress(addr), sizeof(u32)))
            {
                const u32 current = read<u32>(addr);
                if (current == 0)
                {
                    write<u32>(addr, saved);
                }
            }
        }
    }
}

} // namespace runtime
} // namespace psxrecomp
