#include "psxrecomp/runtime/interrupt_dispatcher.h"
#include "psxrecomp/runtime/logger.h"

#include "irq_trace_utils.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
/// All IRQ lines in priority order (VBlank first).
constexpr InterruptLine ALL_LINES[] = {
    InterruptLine::VBlank, InterruptLine::Gpu,    InterruptLine::Cdrom,  InterruptLine::Dma,
    InterruptLine::Timer0, InterruptLine::Timer1, InterruptLine::Timer2, InterruptLine::Controller,
    InterruptLine::Sio,    InterruptLine::Spu,    InterruptLine::Pio,
};
constexpr size_t NUM_LINES = sizeof(ALL_LINES) / sizeof(ALL_LINES[0]);

} // namespace

InterruptDispatcher::InterruptDispatcher()
{
    reset();
}

void InterruptDispatcher::reset()
{
    m_callbacksDispatched = 0;
    m_dispatching = false;
    m_deferredCallbacks.clear();
}

void InterruptDispatcher::setCallbackInvoker(CallbackInvoker invoker)
{
    m_invoker = std::move(invoker);
}

void InterruptDispatcher::serviceInterrupts(InterruptController& interrupts,
                                            KernelEventTable& events, u32 criticalDepth,
                                            RuntimeLogger* logger)
{
    serviceInterruptsImpl(interrupts, events, criticalDepth, logger, 0, false);
}

void InterruptDispatcher::servicePendingMask(InterruptController& interrupts,
                                             KernelEventTable& events, u32 criticalDepth,
                                             u32 pendingMask, RuntimeLogger* logger)
{
    serviceInterruptsImpl(interrupts, events, criticalDepth, logger, pendingMask, true);
}

void InterruptDispatcher::serviceInterruptsImpl(InterruptController& interrupts,
                                                KernelEventTable& events, u32 criticalDepth,
                                                RuntimeLogger* logger, u32 pendingMask,
                                                bool hasForcedPendingMask)
{
    if (traceIrqFlowEnabled() && logger)
    {
        std::ostringstream msg;
        msg << "event=dispatcher_enter status=0x" << std::hex << interrupts.readStatus()
            << " mask=0x" << interrupts.readMask() << " critical_depth=" << std::dec
            << criticalDepth << " reentrant=" << (m_dispatching ? 1 : 0);
        if (hasForcedPendingMask)
        {
            msg << " forced_pending=0x" << std::hex << pendingMask;
        }
        logger->log(LogLevel::Info, "irq_trace", msg.str());
    }

    // Reentrancy guard: if we are already inside a callback dispatch,
    // defer any new callbacks.
    if (m_dispatching)
    {
        if (traceIrqFlowEnabled() && logger)
        {
            logger->log(LogLevel::Info, "irq_trace", "event=dispatcher_skip reason=reentrant");
        }
        return;
    }

    // If in a critical section, defer delivery.
    if (criticalDepth > 0)
    {
        if (traceIrqFlowEnabled() && logger)
        {
            logger->log(LogLevel::Info, "irq_trace",
                        "event=dispatcher_skip reason=critical_section");
        }
        return;
    }

    const auto flushDeferredCallbacks = [this, logger](const char* source)
    {
        if (m_deferredCallbacks.empty() || !m_invoker)
        {
            return;
        }

        m_dispatching = true;
        size_t drained = 0;
        try
        {
            for (; drained < m_deferredCallbacks.size(); ++drained)
            {
                const u32 addr = m_deferredCallbacks[drained];
                if (traceIrqFlowEnabled() && logger)
                {
                    std::ostringstream msg;
                    msg << "event=irq_callback_invoke source=" << source << " addr=0x" << std::hex
                        << addr;
                    logger->log(LogLevel::Info, "irq_trace", msg.str());
                }
                m_invoker(addr);
                ++m_callbacksDispatched;
            }
        }
        catch (...)
        {
            if (drained > 0)
            {
                m_deferredCallbacks.erase(m_deferredCallbacks.begin(),
                                          m_deferredCallbacks.begin() + drained);
            }
            m_dispatching = false;
            throw;
        }

        m_deferredCallbacks.clear();
        m_dispatching = false;
    };

    if (!hasForcedPendingMask && !interrupts.isInterruptPending())
    {
        // No pending work — but flush any deferred callbacks from a
        // previous critical section.
        flushDeferredCallbacks("deferred_flush");
        return;
    }

    const u32 status = interrupts.readStatus();
    const u32 mask = interrupts.readMask();
    const u32 pending = hasForcedPendingMask ? (pendingMask & mask) : (status & mask);
    if (pending == 0)
    {
        return;
    }

    m_dispatching = true;
    try
    {
        u32 remainingBudget = m_maxCallbacksPerService;
        for (size_t i = 0; i < NUM_LINES && remainingBudget > 0; ++i)
        {
            const u16 bit = static_cast<u16>(ALL_LINES[i]);
            if ((pending & bit) == 0)
            {
                continue;
            }
            if (traceIrqFlowEnabled() && logger)
            {
                std::ostringstream msg;
                msg << "event=irq_dispatch_order index=" << std::dec << i << " line=0x" << std::hex
                    << bit << " remaining_budget=" << std::dec << remainingBudget;
                logger->log(LogLevel::Info, "irq_trace", msg.str());
            }
            const u32 dispatched =
                dispatchLine(ALL_LINES[i], interrupts, events, logger, remainingBudget);
            remainingBudget -= dispatched;
        }
    }
    catch (...)
    {
        m_dispatching = false;
        throw;
    }
    m_dispatching = false;

    // Drain deferred callbacks that accumulated during dispatch (e.g. if
    // a callback raised a new interrupt that was serviced recursively).
    flushDeferredCallbacks("post_dispatch_flush");
}

u32 InterruptDispatcher::callbacksDispatched() const
{
    return m_callbacksDispatched;
}

void InterruptDispatcher::setMaxCallbacksPerService(u32 max)
{
    m_maxCallbacksPerService = max;
}

u32 InterruptDispatcher::dispatchLine(InterruptLine line, InterruptController& interrupts,
                                      KernelEventTable& events, RuntimeLogger* logger,
                                      u32 remaining)
{
    if (traceIrqFlowEnabled() && logger)
    {
        std::ostringstream msg;
        msg << "event=irq_dispatch_begin line=0x" << std::hex << static_cast<u16>(line)
            << " status=0x" << interrupts.readStatus() << " mask=0x" << interrupts.readMask()
            << " remaining=" << std::dec << remaining;
        logger->log(LogLevel::Info, "irq_trace", msg.str());
    }

    const u32 eventClass = KernelEventTable::interruptLineToEventClass(line);
    if (eventClass == 0)
    {
        return 0;
    }

    // Deliver events for the "Interrupted" spec (most common for HW IRQs).
    auto callbacks = events.deliverByClassSpec(eventClass, EventSpec::Interrupted);

    // Some BIOS/libetc code registers VBlank callbacks through the alternate
    // class 0xF2000002 instead of the canonical 0xF0000001 event family.
    // On hardware these callbacks are still driven by the VBlank IRQ.
    if (line == InterruptLine::VBlank)
    {
        auto alternateCallbacks =
            events.deliverByClassSpec(EventClass::VBlankAlt, EventSpec::Interrupted);
        callbacks.insert(callbacks.end(), alternateCallbacks.begin(), alternateCallbacks.end());
    }

    // Also deliver "Counter" events for timer lines (used by VSync/timer
    // counter patterns).
    if (line == InterruptLine::VBlank || line == InterruptLine::Timer0 ||
        line == InterruptLine::Timer1 || line == InterruptLine::Timer2)
    {
        auto counterCallbacks = events.deliverByClassSpec(eventClass, EventSpec::Counter);
        callbacks.insert(callbacks.end(), counterCallbacks.begin(), counterCallbacks.end());
    }

    // Acknowledge the hardware IRQ line now that events have been delivered.
    // I_STAT clears bits written as 0, so clear this line by writing all-ones
    // except the target bit.
    interrupts.writeStatus(~static_cast<u32>(line));

    if (traceIrqFlowEnabled() && logger)
    {
        std::ostringstream msg;
        msg << "event=irq_dispatch_ack line=0x" << std::hex << static_cast<u16>(line)
            << " status_after=0x" << interrupts.readStatus();
        logger->log(LogLevel::Info, "irq_trace", msg.str());
    }

    if (callbacks.empty())
    {
        return 0;
    }

    if (logger)
    {
        std::ostringstream msg;
        msg << "IRQ dispatch: line=0x" << std::hex << static_cast<u16>(line)
            << " callbacks=" << std::dec << callbacks.size();
        logger->log(LogLevel::Debug, "irq", msg.str());
    }

    u32 invoked = 0;
    for (u32 addr : callbacks)
    {
        if (invoked >= remaining)
        {
            // Budget exceeded — defer remaining callbacks.
            if (traceIrqFlowEnabled() && logger)
            {
                std::ostringstream msg;
                msg << "event=irq_callback_deferred line=0x" << std::hex << static_cast<u16>(line)
                    << " addr=0x" << addr;
                logger->log(LogLevel::Info, "irq_trace", msg.str());
            }
            m_deferredCallbacks.push_back(addr);
            continue;
        }
        if (m_invoker)
        {
            if (traceIrqFlowEnabled() && logger)
            {
                std::ostringstream msg;
                msg << "event=irq_callback_invoke source=dispatch line=0x" << std::hex
                    << static_cast<u16>(line) << " addr=0x" << addr;
                logger->log(LogLevel::Info, "irq_trace", msg.str());
            }
            m_invoker(addr);
            ++m_callbacksDispatched;
            ++invoked;
        }
        else if (traceIrqFlowEnabled() && logger)
        {
            std::ostringstream msg;
            msg << "event=irq_callback_skipped line=0x" << std::hex << static_cast<u16>(line)
                << " addr=0x" << addr << " reason=no_invoker";
            logger->log(LogLevel::Info, "irq_trace", msg.str());
        }
    }
    return invoked;
}

} // namespace runtime
} // namespace psxrecomp
