#include "psxrecomp/runtime/psx_system.h"

#include "irq_trace_utils.h"

#include <sstream>
#include <utility>

namespace psxrecomp
{
namespace runtime
{

namespace
{

constexpr u32 STATUS_IEC_BIT = 1u << 0;
constexpr u32 STATUS_IM0_IM1_MASK = 0x00000300u;
constexpr u32 CAUSE_IP0_IP1_MASK = 0x00000300u;

const char* cpuExecutionPhaseName(PsxSystem::CpuExecutionPhase phase)
{
    switch (phase)
    {
    case PsxSystem::CpuExecutionPhase::Reset:
        return "reset";
    case PsxSystem::CpuExecutionPhase::BootInitializing:
        return "boot_initializing";
    case PsxSystem::CpuExecutionPhase::AwaitingExecutableEntry:
        return "awaiting_entry";
    case PsxSystem::CpuExecutionPhase::Running:
        return "running";
    default:
        return "unknown";
    }
}

class IrqExceptionExitGuard
{
  public:
    explicit IrqExceptionExitGuard(Cop0& cop0) : m_cop0(cop0) {}
    IrqExceptionExitGuard(const IrqExceptionExitGuard&) = delete;
    IrqExceptionExitGuard& operator=(const IrqExceptionExitGuard&) = delete;
    ~IrqExceptionExitGuard()
    {
        m_cop0.rfe();
    }

  private:
    Cop0& m_cop0;
};
} // namespace

void PsxSystem::serviceIrqWork(u32 pendingMasked)
{
    u32 pendingForHook = pendingMasked;
    u32 pendingForKernelEvents = pendingMasked;

    // PSX-SPX: ChangeClearRCnt(t, flag=1) causes the kernel to automatically
    // acknowledge timer/VBlank IRQs and immediately return from exception,
    // skipping chain handlers and HookEntryInt for those sources.
    {
        constexpr std::pair<InterruptLine, size_t> autoClearSources[] = {
            {InterruptLine::Timer0, 0},
            {InterruptLine::Timer1, 1},
            {InterruptLine::Timer2, 2},
            {InterruptLine::VBlank, 3},
        };
        for (const auto& [line, policyIndex] : autoClearSources)
        {
            const u32 bit = static_cast<u32>(line);
            if ((pendingForHook & bit) != 0 && m_changeClearRCntPolicy[policyIndex])
            {
                m_interrupts.writeStatus(~bit);
                pendingForHook &= ~bit;
                pendingForKernelEvents &= ~bit;
                if (traceIrqFlowEnabled())
                {
                    std::ostringstream msg;
                    msg << "event=auto_clear_rcnt line=0x" << std::hex << bit
                        << " policy_index=" << std::dec << policyIndex;
                    m_logger.log(LogLevel::Info, "irq_trace", msg.str());
                }
            }
        }
        syncCop0InterruptPending();
        if (pendingForHook == 0u && pendingForKernelEvents == 0u)
        {
            return;
        }
    }

    // PSX-accurate ordering: walk SysEnqIntRP priority chains before HookEntryInt.
    // On real hardware the BIOS exception handler invokes the linked-list
    // handlers installed by SysEnqIntRP(priority, struc) *before* transferring
    // control to HookEntryInt. Chain handlers (e.g., CDROM interrupt handler
    // registered at priority 2) read hardware registers, acknowledge their IRQ
    // source, and update game state so that the HookEntryInt handler only has
    // to deal with software-level dispatch of the remaining pending set.
    if (pendingForHook != 0u)
    {
        bool chainFiredRfe = false;
        try
        {
            chainFiredRfe = dispatchIrqChains();
        }
        catch (const ReturnFromExceptionSignal&)
        {
            // Should not happen (dispatchIrqChains already catches RFE), but
            // handle defensively.
            syncCop0InterruptPending();
            return;
        }

        if (chainFiredRfe)
        {
            if (traceIrqFlowEnabled())
            {
                m_logger.log(LogLevel::Info, "irq_trace",
                             "event=return_from_exception source=irq_chain_pre_hook");
            }
            // PSX-SPX: ReturnFromException from a chain handler aborts the
            // entire exception flow. No kernel events or HookEntryInt run.
            syncCop0InterruptPending();
            return;
        }

        // Chain handlers may have acknowledged interrupts; re-read the mask.
        syncCop0InterruptPending();
        pendingForHook = m_interrupts.readStatus() & m_interrupts.readMask();
    }

    // PSX-SPX: the BIOS CD-ROM IRQ handlers live in the priority-0 chain, so
    // dispatchIrqChains() has already run them before lower priorities and
    // HookEntryInt. Exclude IRQ2 from the generic kernel-event dispatcher here:
    // serviceBiosCdromInterrupt() already delivered the F0000003 sub-events.
    pendingForKernelEvents = pendingForHook & ~static_cast<u32>(InterruptLine::Cdrom);

    const u32 pendingAfterChains = m_interrupts.readStatus() & m_interrupts.readMask();

    if (pendingAfterChains != 0 && traceIrqFlowEnabled())
    {
        std::ostringstream msg;
        msg << "event=irq_dispatch_order source=service_interrupts pending_masked=0x" << std::hex
            << pendingAfterChains;
        m_logger.log(LogLevel::Info, "irq_trace", msg.str());
    }

    // Kernel event delivery (OpenEvent/EnableEvent model).
    // Runs before HookEntryInt: PSX-accurate ordering matches the BIOS exception
    // handler which dispatches kernel events prior to calling HookEntryInt.
    // A callback may execute ReturnFromException to abort further handling.
    // COP0 Status restore is handled by the caller's epilogue.
    m_cpTimeline.push(CpEventKind::DispatcherEnter, 0, pendingForKernelEvents,
                      m_interrupts.readStatus(), m_interrupts.readMask());
    try
    {
        m_dispatcher.servicePendingMask(m_interrupts, m_events, m_criticalSectionDepth,
                                        pendingForKernelEvents, &m_logger);
    }
    catch (const ReturnFromExceptionSignal&)
    {
        if (traceIrqFlowEnabled())
        {
            m_logger.log(LogLevel::Info, "irq_trace",
                         "event=return_from_exception source=dispatcher_pending");
        }
        m_cpTimeline.push(CpEventKind::DispatcherExit, 1, m_dispatcher.callbacksDispatched(),
                          m_interrupts.readStatus(), 0);
        syncCop0InterruptPending();
        return;
    }
    if (m_dispatcher.callbacksDispatched() > 0)
    {
        m_cpTimeline.push(CpEventKind::DispatcherAck, 0, m_dispatcher.callbacksDispatched(),
                          pendingForKernelEvents, m_interrupts.readStatus());
    }
    m_cpTimeline.push(CpEventKind::DispatcherExit, 0, m_dispatcher.callbacksDispatched(),
                      m_interrupts.readStatus(), 0);

    // HookEntryInt runs after kernel events. Demo SDKs use it as their primary
    // hardware IRQ fan-out path. IRQ status bits must still be visible here
    // so the game handler can determine which interrupt fired.
    if (pendingForHook != 0u && m_criticalSectionDepth == 0 &&
        m_hookEntryInt.descriptorAddress != 0 && !m_inHookEntryIntHandler)
    {
        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=hook_entry_int_invoke descriptor=0x" << std::hex
                << m_hookEntryInt.descriptorAddress;
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        try
        {
            invokeHookEntryIntHandler();
        }
        catch (const ReturnFromExceptionSignal&)
        {
            if (traceIrqFlowEnabled())
            {
                m_logger.log(LogLevel::Info, "irq_trace",
                             "event=return_from_exception source=hook_entry_int");
            }
            // PSX-SPX: RFE from HookEntryInt is the handler's explicit exit.
            // The handler is responsible for clearing I_STAT bits it processed.
            // Do NOT blanket-clear here — doing so would mask IRQs that the
            // handler deliberately left pending for re-delivery.
            syncCop0InterruptPending();
            return;
        }
    }

    // PSX-SPX: do not blanket-clear I_STAT after service.  Chain handlers,
    // kernel events, and HookEntryInt each write I_STAT themselves when they
    // acknowledge an interrupt.  Clearing bits here would incorrectly suppress
    // IRQs that the game's handlers left pending for re-delivery.
    syncCop0InterruptPending();
}

void PsxSystem::serviceInterrupts()
{
    syncLevelInterruptSources();

    // Record CD-ROM / IRQ state snapshot for stall classification.
    m_stallClassifier.recordCdromIrqState(
        m_cdrom.readInterruptFlags(), static_cast<u16>(m_interrupts.readStatus()),
        static_cast<u16>(m_interrupts.readMask()), m_cdrom.hasIrqRequest());

    const u32 pendingMasked = m_interrupts.readStatus() & m_interrupts.readMask();
    const u32 cop0Status = m_cop0.mfc0(Cop0::RegisterIndex::Status);
    const u32 cop0Cause = m_cop0.mfc0(Cop0::RegisterIndex::Cause);
    const u32 swPendingMasked =
        (cop0Cause & CAUSE_IP0_IP1_MASK) & (cop0Status & STATUS_IM0_IM1_MASK);
    const bool swPending = (cop0Status & STATUS_IEC_BIT) != 0u && swPendingMasked != 0u;
    const bool cop0IrqPending = pendingMasked != 0u || swPending;
    const bool irqTakeEligible = m_cop0.shouldTakeInterruptException();
    const bool executionLive = m_cpuExecutionPhase == CpuExecutionPhase::Running;

    // Fresh exception entry: COP0 must be ready to take an interrupt and the
    // machine must not already be inside BIOS exception handling.
    // PSX-SPX explicitly states the kernel does not support nested exceptions,
    // so we never call exceptionEnter() while m_inCallbackInvocation is true.
    const bool canEnterFreshIrqException = cop0IrqPending && executionLive &&
                                           m_criticalSectionDepth == 0 && !m_inCallbackInvocation &&
                                           irqTakeEligible;

    if (traceIrqFlowEnabled())
    {
        std::ostringstream msg;
        msg << "event=service_interrupts pending_masked=0x" << std::hex << pendingMasked
            << " sw_pending_masked=0x" << swPendingMasked << " status=0x"
            << m_interrupts.readStatus() << " mask=0x" << m_interrupts.readMask()
            << " critical_depth=" << std::dec << m_criticalSectionDepth
            << " in_callback=" << (m_inCallbackInvocation ? 1 : 0)
            << " phase=" << cpuExecutionPhaseName(m_cpuExecutionPhase)
            << " cop0_sw_pending=" << (swPending ? 1 : 0)
            << " cop0_irq_take_eligible=" << (irqTakeEligible ? 1 : 0)
            << " execution_live=" << (executionLive ? 1 : 0)
            << " can_enter_fresh=" << (canEnterFreshIrqException ? 1 : 0);
        m_logger.log(LogLevel::Info, "irq_trace", msg.str());
    }

    if (!cop0IrqPending)
    {
        // Still allow the event dispatcher to flush deferred callbacks.
        m_cpTimeline.push(CpEventKind::DispatcherEnter, 0, 0,
                          m_interrupts.readStatus(), m_interrupts.readMask());
        try
        {
            m_dispatcher.serviceInterrupts(m_interrupts, m_events, m_criticalSectionDepth,
                                           &m_logger);
        }
        catch (const ReturnFromExceptionSignal&)
        {
            if (traceIrqFlowEnabled())
            {
                m_logger.log(LogLevel::Info, "irq_trace",
                             "event=return_from_exception source=dispatcher_empty_pending");
            }
            m_cpTimeline.push(CpEventKind::DispatcherExit, 1, m_dispatcher.callbacksDispatched(),
                              m_interrupts.readStatus(), 0);
            syncCop0InterruptPending();
            return;
        }
        m_cpTimeline.push(CpEventKind::DispatcherExit, 0, m_dispatcher.callbacksDispatched(),
                          m_interrupts.readStatus(), 0);
        syncCop0InterruptPending();
        return;
    }

    if (!canEnterFreshIrqException)
    {
        // Keep pending state visible via Cause.IP bits but do not dispatch.
        // PSX-SPX: the kernel does not support nested exceptions.  When we
        // are already inside BIOS exception handling (m_inCallbackInvocation)
        // or COP0 masks prevent a new entry, or boot has not yet handed off
        // to executable code, we must not call exceptionEnter()
        // again.  The IRQ will be serviced on the next call from outside the
        // current exception flow.
        syncCop0InterruptPending();
        return;
    }

    // Enter a fresh COP0 interrupt exception and service all pending work.
    // rfe() is issued by IrqExceptionExitGuard when this scope exits.
    m_cop0.exceptionEnter(Cop0::ExceptionCode::Interrupt, architecturalProgramCounter(), false);
    IrqExceptionExitGuard irqExitGuard(m_cop0);
    serviceIrqWork(pendingMasked);
}

PsxSystem::CallbackContextDisposition
PsxSystem::consumePendingCallbackRegisters(std::array<u32, 32>& regsInOut)
{
    if (!m_hasPendingCallbackRegisters)
    {
        return CallbackContextDisposition::RestoreSaved;
    }

    for (size_t reg = 0; reg < m_pendingCallbackRegisters.size(); ++reg)
    {
        if (m_pendingCallbackRegisterMask[reg])
        {
            regsInOut[reg] = m_pendingCallbackRegisters[reg];
        }
    }
    m_hookEntryIntTrace.noteCommittedResume(m_pendingCallbackRegisters[31]);
    m_hasPendingCallbackRegisters = false;
    m_pendingCallbackRegisterMask.fill(false);
    ++m_callbackContextCommitGeneration;
    return CallbackContextDisposition::CommitMutated;
}

u32 PsxSystem::callbackContextCommitGeneration() const
{
    return m_callbackContextCommitGeneration;
}

const RuntimeControlPlaneTimeline& PsxSystem::cpTimeline() const
{
    return m_cpTimeline;
}

} // namespace runtime
} // namespace psxrecomp
