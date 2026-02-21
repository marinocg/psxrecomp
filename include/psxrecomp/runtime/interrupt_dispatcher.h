#pragma once

#include "psxrecomp/runtime/interrupt_controller.h"
#include "psxrecomp/runtime/kernel_events.h"
#include "psxrecomp/types.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class RuntimeLogger;

/**
 * @brief Callback invoker signature.
 *
 * The runtime binds this to the generated module's
 * `callRecompiledFunction(context, address)` bridge so that the
 * interrupt dispatcher can invoke PSX callback code without any
 * knowledge of the recompiled-module layout.
 */
using CallbackInvoker = std::function<void(u32 address)>;

/**
 * @brief Dispatches pending hardware interrupts to kernel events.
 *
 * The dispatcher sits between the hardware InterruptController (which
 * tracks which IRQ lines are asserted) and the KernelEventTable (which
 * tracks which software events the game has registered).  At safe
 * points during execution the generated code calls
 * `serviceInterrupts()` which:
 *
 * 1. Checks pending & masked interrupt lines.
 * 2. For each asserted line, delivers matching kernel events.
 * 3. Invokes callback-mode event addresses via the CallbackInvoker.
 * 4. Acknowledges the hardware IRQ line after delivery.
 *
 * The dispatcher respects the critical-section depth so that callbacks
 * are deferred while the game is inside EnterCriticalSection.
 */
class InterruptDispatcher
{
  public:
    InterruptDispatcher();

    /**
     * @brief Reset dispatcher state (clears deferred queue).
     */
    void reset();

    /**
     * @brief Install the callback invoker bridge.
     *
     * Must be called before any serviceInterrupts() call that might
     * need to invoke recompiled callback code.
     */
    void setCallbackInvoker(CallbackInvoker invoker);

    /**
     * @brief Check and dispatch pending interrupts.
     *
     * @param interrupts    Hardware interrupt controller.
     * @param events        Kernel event table.
     * @param criticalDepth Current critical-section nesting depth.
     * @param logger        Logger for diagnostic output (may be nullptr).
     *
     * This is the primary entry point called from generated code via
     * `serviceInterrupts(context)`.
     */
    void serviceInterrupts(InterruptController& interrupts, KernelEventTable& events,
                           u32 criticalDepth, RuntimeLogger* logger);

    /**
     * @brief Number of callbacks dispatched since last reset.
     */
    u32 callbacksDispatched() const;

    /**
     * @brief Maximum callbacks to invoke per serviceInterrupts call.
     *
     * A bound prevents runaway callback chains from starving the main
     * execution loop.  Default is 8.
     */
    void setMaxCallbacksPerService(u32 max);

  private:
    CallbackInvoker m_invoker;
    u32 m_callbacksDispatched = 0;
    u32 m_maxCallbacksPerService = 8;
    bool m_dispatching = false; ///< Reentrancy guard.

    /// Queued callback addresses deferred due to critical section or
    /// reentrancy.
    std::vector<u32> m_deferredCallbacks;

    /**
     * @brief Dispatch a single interrupt line.
     *
     * Delivers matching events and invokes callbacks, up to the per-service
     * limit.
     *
     * @return Number of callbacks invoked.
     */
    u32 dispatchLine(InterruptLine line, InterruptController& interrupts, KernelEventTable& events,
                     RuntimeLogger* logger, u32 remaining);
};

} // namespace runtime
} // namespace psxrecomp
