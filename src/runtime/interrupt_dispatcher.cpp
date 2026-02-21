#include "psxrecomp/runtime/interrupt_dispatcher.h"
#include "psxrecomp/runtime/logger.h"

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
    // Reentrancy guard: if we are already inside a callback dispatch,
    // defer any new callbacks.
    if (m_dispatching)
    {
        return;
    }

    // If in a critical section, defer delivery.
    if (criticalDepth > 0)
    {
        return;
    }

    if (!interrupts.isInterruptPending())
    {
        // No pending work — but flush any deferred callbacks from a
        // previous critical section.
        if (!m_deferredCallbacks.empty() && m_invoker)
        {
            m_dispatching = true;
            for (u32 addr : m_deferredCallbacks)
            {
                m_invoker(addr);
                ++m_callbacksDispatched;
            }
            m_deferredCallbacks.clear();
            m_dispatching = false;
        }
        return;
    }

    const u32 status = interrupts.readStatus();
    const u32 mask = interrupts.readMask();
    const u32 pending = status & mask;
    if (pending == 0)
    {
        return;
    }

    m_dispatching = true;
    u32 remainingBudget = m_maxCallbacksPerService;

    for (size_t i = 0; i < NUM_LINES && remainingBudget > 0; ++i)
    {
        const u16 bit = static_cast<u16>(ALL_LINES[i]);
        if ((pending & bit) == 0)
        {
            continue;
        }
        const u32 dispatched =
            dispatchLine(ALL_LINES[i], interrupts, events, logger, remainingBudget);
        remainingBudget -= dispatched;
    }

    m_dispatching = false;

    // Drain deferred callbacks that accumulated during dispatch (e.g. if
    // a callback raised a new interrupt that was serviced recursively).
    if (!m_deferredCallbacks.empty() && m_invoker)
    {
        m_dispatching = true;
        for (u32 addr : m_deferredCallbacks)
        {
            m_invoker(addr);
            ++m_callbacksDispatched;
        }
        m_deferredCallbacks.clear();
        m_dispatching = false;
    }
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
    const u32 eventClass = KernelEventTable::interruptLineToEventClass(line);
    if (eventClass == 0)
    {
        return 0;
    }

    // Deliver events for the "Interrupted" spec (most common for HW IRQs).
    auto callbacks = events.deliverByClassSpec(eventClass, EventSpec::Interrupted);

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
            m_deferredCallbacks.push_back(addr);
            continue;
        }
        if (m_invoker)
        {
            m_invoker(addr);
            ++m_callbacksDispatched;
            ++invoked;
        }
    }
    return invoked;
}

} // namespace runtime
} // namespace psxrecomp
