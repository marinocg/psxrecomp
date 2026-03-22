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
    case PsxSystem::CpuExecutionPhase::Bootstrapping:
        return "bootstrapping";
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
        pendingForKernelEvents = pendingForHook;
    }

    // PSX-SPX: the BIOS CD-ROM IRQ handlers live in the priority-0 chain, so
    // dispatchIrqChains() has already run them before lower priorities and
    // HookEntryInt. Exclude IRQ2 from the generic kernel-event dispatcher here:
    // serviceBiosCdromInterrupt() already delivered the F0000003 sub-events.
    pendingForKernelEvents = pendingForHook & ~static_cast<u32>(InterruptLine::Cdrom);

    // PSX-accurate interrupt ordering: on real hardware, CDROM sectors arrive
    // between VBlanks so the CDROM handler fires and processes data before the
    // next VBlank handler polls for it.  The batched-cycle model in the
    // recompiler can cause CDROM INT1 and VBlank to become pending in the
    // same serviceInterrupts() call.  When both are visible in I_STAT, a game
    // handler that iterates bits from low to high would process VBlank (bit 0)
    // before CDROM (bit 2), causing decompression polling to miss the data.
    //
    // Fix: when VBlank and at least one non-VBlank IRQ are pending together,
    // deliver the non-VBlank interrupts first in a separate HookEntryInt
    // invocation.  The game's CDROM handler runs, DMA's sector data, and
    // updates its ring-buffer status.  Then VBlank is restored and delivered
    // normally so the VBlank handler finds the data ready.
    if (pendingForHook != 0u && m_criticalSectionDepth == 0 &&
        m_hookEntryInt.descriptorAddress != 0 && !m_inHookEntryIntHandler)
    {
        constexpr u32 vblankBit = static_cast<u32>(InterruptLine::VBlank);
        const bool hasVBlank = (pendingForHook & vblankBit) != 0;
        const bool hasOtherIrqs = (pendingForHook & ~vblankBit) != 0;

        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=split_check pendingForHook=0x" << std::hex << pendingForHook
                << " hasVBlank=" << hasVBlank << " hasOtherIrqs=" << hasOtherIrqs;
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }

        if (hasVBlank && hasOtherIrqs)
        {
            // Phase 1: deliver non-VBlank interrupts only.
            m_interrupts.writeStatus(~vblankBit);
            syncCop0InterruptPending();

            if (traceIrqFlowEnabled())
            {
                std::ostringstream msg;
                msg << "event=hook_entry_int_invoke_split phase=non_vblank descriptor=0x"
                    << std::hex << m_hookEntryInt.descriptorAddress << " pending=0x"
                    << (m_interrupts.readStatus() & m_interrupts.readMask());
                m_logger.log(LogLevel::Info, "irq_trace", msg.str());
            }

            try
            {
                invokeHookEntryIntHandler();
            }
            catch (const ReturnFromExceptionSignal&)
            {
                // First callback completed via ReturnFromException.
                // COP0 is still in exception mode; the caller's
                // IrqExceptionExitGuard (or the in-flight depth guard) handles
                // cleanup.
            }

            // Phase 2: restore VBlank for normal delivery below.
            m_interrupts.raise(InterruptLine::VBlank);
            syncCop0InterruptPending();
            pendingForHook = m_interrupts.readStatus() & m_interrupts.readMask();
        }
    }

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
        syncCop0InterruptPending();
        return;
    }

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
            // Kernel events were already delivered above; no re-delivery needed.
            // Acknowledge any IRQ bits that were pending when this exception
            // entered — the BIOS RFE path effectively drains them.
            for (u32 bit = 1; bit < (1u << 11); bit <<= 1)
            {
                if ((pendingForHook & bit) != 0)
                {
                    m_interrupts.writeStatus(~bit);
                }
            }
            syncCop0InterruptPending();
            return;
        }
    }

    // Acknowledge remaining IRQ bits that were pending when this exception
    // started.  On real hardware the chain handlers and HookEntryInt code
    // would have written I_STAT to clear them; in the recompiled environment
    // the game's MMIO writes may not reach our interrupt controller model,
    // so we clean up here to prevent infinite re-delivery.
    for (u32 bit = 1; bit < (1u << 11); bit <<= 1)
    {
        if ((pendingForHook & bit) != 0)
        {
            m_interrupts.writeStatus(~bit);
        }
    }
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
            syncCop0InterruptPending();
            return;
        }
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

} // namespace runtime
} // namespace psxrecomp
