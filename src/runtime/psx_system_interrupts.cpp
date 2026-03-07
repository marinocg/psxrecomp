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
const char* interruptLineName(InterruptLine line)
{
    switch (line)
    {
    case InterruptLine::VBlank:
        return "VBlank";
    case InterruptLine::Gpu:
        return "Gpu";
    case InterruptLine::Cdrom:
        return "Cdrom";
    case InterruptLine::Dma:
        return "Dma";
    case InterruptLine::Timer0:
        return "Timer0";
    case InterruptLine::Timer1:
        return "Timer1";
    case InterruptLine::Timer2:
        return "Timer2";
    case InterruptLine::Controller:
        return "Controller";
    case InterruptLine::Sio:
        return "Sio";
    case InterruptLine::Spu:
        return "Spu";
    case InterruptLine::Pio:
        return "Pio";
    default:
        return "Unknown";
    }
}

std::string formatPendingLineOrder(u32 pendingMasked)
{
    constexpr InterruptLine kPriorityOrder[] = {
        InterruptLine::VBlank, InterruptLine::Gpu,        InterruptLine::Cdrom,
        InterruptLine::Dma,    InterruptLine::Timer0,     InterruptLine::Timer1,
        InterruptLine::Timer2, InterruptLine::Controller, InterruptLine::Sio,
        InterruptLine::Spu,    InterruptLine::Pio,
    };

    std::ostringstream out;
    bool first = true;
    for (InterruptLine line : kPriorityOrder)
    {
        const u32 bit = static_cast<u32>(line);
        if ((pendingMasked & bit) == 0)
        {
            continue;
        }
        if (!first)
        {
            out << ",";
        }
        out << interruptLineName(line);
        first = false;
    }

    return out.str();
}

constexpr size_t REG_V0 = 2;
constexpr size_t REG_S0 = 16;
constexpr size_t REG_S7 = 23;
constexpr size_t REG_GP = 28;
constexpr size_t REG_SP = 29;
constexpr size_t REG_FP = 30;
constexpr size_t REG_RA = 31;
constexpr u32 STATUS_IEC_BIT = 1u << 0;
constexpr u32 STATUS_IM0_IM1_MASK = 0x00000300u;
constexpr u32 CAUSE_IP0_IP1_MASK = 0x00000300u;
constexpr std::array<u16, 5> CDROM_IRQ_EVENT_SPECS = {
    0x0010u, // INT1 -> data-ready style event
    0x0020u, // INT2 -> command completion
    0x0020u, // INT3 -> command completion for single-response commands (eg. Getstat)
    0x0080u, // INT4 -> end-of-read style event
    0x8000u, // INT5 -> error
};

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

bool PsxSystem::serviceBiosCdromInterrupt()
{
    if (!m_biosCdrom.initialized || !m_cdrom.hasIrqRequest())
    {
        return false;
    }

    const u8 irqType = static_cast<u8>(m_cdrom.readInterruptFlags() & 0x07u);
    if (irqType < 1u || irqType > CDROM_IRQ_EVENT_SPECS.size())
    {
        return false;
    }

    std::vector<u32> callbacks =
        m_events.deliverByClassSpec(EventClass::Cdrom, CDROM_IRQ_EVENT_SPECS[irqType - 1u]);
    auto genericCallbacks = m_events.deliverByClassSpec(EventClass::Cdrom, EventSpec::Interrupted);
    callbacks.insert(callbacks.end(), genericCallbacks.begin(), genericCallbacks.end());

    for (u32 address : callbacks)
    {
        invokeCallback(address);
    }

    return true;
}

void PsxSystem::setCallbackInvoker(CallbackInvoker invoker)
{
    // Store the raw invoker bridge (calls into the generated module).
    m_callbackInvoker = std::move(invoker);

    // The dispatcher should invoke callbacks through the system so that
    // ReturnFromException works (requires m_inCallbackInvocation=true).
    m_dispatcher.setCallbackInvoker([this](u32 address) -> u32
                                    { return this->invokeCallbackRaw(address); });
}

void PsxSystem::serviceInterrupts()
{
    syncLevelInterruptSources();

    const u32 pendingMasked = m_interrupts.readStatus() & m_interrupts.readMask();
    const u32 cop0Status = m_cop0.mfc0(Cop0::RegisterIndex::Status);
    const u32 cop0Cause = m_cop0.mfc0(Cop0::RegisterIndex::Cause);
    const u32 swPendingMasked =
        (cop0Cause & CAUSE_IP0_IP1_MASK) & (cop0Status & STATUS_IM0_IM1_MASK);
    const bool swPending = (cop0Status & STATUS_IEC_BIT) != 0u && swPendingMasked != 0u;
    const bool cop0IrqPending = pendingMasked != 0u || swPending;
    const bool irqTakeEligible = m_cop0.shouldTakeInterruptException();
    const bool irqDeliveryEligible =
        cop0IrqPending && m_criticalSectionDepth == 0 && !m_inCallbackInvocation && irqTakeEligible;
    if (traceIrqFlowEnabled())
    {
        std::ostringstream msg;
        msg << "event=service_interrupts pending_masked=0x" << std::hex << pendingMasked
            << " sw_pending_masked=0x" << swPendingMasked << " status=0x"
            << m_interrupts.readStatus() << " mask=0x" << m_interrupts.readMask()
            << " critical_depth=" << std::dec << m_criticalSectionDepth
            << " in_callback=" << (m_inCallbackInvocation ? 1 : 0)
            << " cop0_sw_pending=" << (swPending ? 1 : 0)
            << " cop0_irq_take_eligible=" << (irqTakeEligible ? 1 : 0)
            << " irq_delivery_eligible=" << (irqDeliveryEligible ? 1 : 0);
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

    if (!irqDeliveryEligible)
    {
        // Keep pending state visible via Cause.IP bits, but do not dispatch
        // BIOS/IRQ callbacks until COP0 interrupt masks allow delivery.
        syncCop0InterruptPending();
        return;
    }

    m_cop0.exceptionEnter(Cop0::ExceptionCode::Interrupt, m_debugOverlay.lastProgramCounter(),
                          false);
    IrqExceptionExitGuard irqExitGuard(m_cop0);

    u32 pendingForHook = pendingMasked;
    u32 pendingForKernelEvents = pendingMasked;

    // PSX-SPX: CDROM IRQs expose five sub-interrupt types that the BIOS maps
    // to separate kernel events during _96_init. Handle those before any
    // generic kernel-event dispatch. Recompute the remaining hardware-pending
    // mask afterward so HookEntryInt only runs for IRQ lines that are still
    // visible to the BIOS handler.
    if ((pendingForHook & static_cast<u32>(InterruptLine::Cdrom)) != 0u)
    {
        pendingForKernelEvents &= ~static_cast<u32>(InterruptLine::Cdrom);
        try
        {
            (void)serviceBiosCdromInterrupt();
        }
        catch (const ReturnFromExceptionSignal&)
        {
            if (traceIrqFlowEnabled())
            {
                m_logger.log(LogLevel::Info, "irq_trace",
                             "event=return_from_exception source=bios_cdrom_dispatch");
            }
            syncCop0InterruptPending();
            return;
        }

        syncCop0InterruptPending();
        if (!m_cdrom.hasIrqRequest())
        {
            m_interrupts.writeStatus(~static_cast<u32>(InterruptLine::Cdrom));
            syncCop0InterruptPending();
        }
        pendingForHook = m_interrupts.readStatus() & m_interrupts.readMask();
    }

    // HookEntryInt must run while the remaining IRQ status bits are still
    // visible. Demo SDKs use it as their primary hardware IRQ fan-out path.
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
            }
            syncCop0InterruptPending();
            return;
        }
    }

    const u32 pendingAfterHook = m_interrupts.readStatus() & m_interrupts.readMask();

    if (pendingAfterHook != 0 && traceIrqFlowEnabled())
    {
        std::ostringstream msg;
        msg << "event=irq_dispatch_order source=service_interrupts pending_masked=0x" << std::hex
            << pendingAfterHook << " lines=" << formatPendingLineOrder(pendingAfterHook);
        m_logger.log(LogLevel::Info, "irq_trace", msg.str());
    }

    // BIOS-style exception handler priority chains (installed via SysEnqIntRP).
    // These handlers are responsible for updating SDK counters (eg. PSn00bSDK VSync)
    // and for acknowledging IRQ sources.
    if (pendingAfterHook != 0 && dispatchIrqChains())
    {
        if (traceIrqFlowEnabled())
        {
            m_logger.log(LogLevel::Info, "irq_trace",
                         "event=return_from_exception source=irq_chain_dispatch");
        }
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
        }
        syncCop0InterruptPending();
        return;
    }

    // Kernel event delivery (OpenEvent/EnableEvent model).
    // A callback may execute ReturnFromException to abort further handling.
    // COP0 Status restore is handled by this function's IRQ epilogue.
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

    syncCop0InterruptPending();
}

void PsxSystem::invokeHookEntryIntHandler()
{
    m_inHookEntryIntHandler = true;
    try
    {
        u32 resumeAddress = 0;
        const Address descriptorPhysical = normalizeAddress(m_hookEntryInt.descriptorAddress);
        if (m_hookEntryInt.descriptorAddress != 0 &&
            descriptorPhysical <= MemoryMap::RAM_SIZE - 0x30u)
        {
            // HookEntryInt is implemented by BIOS as longjmp(setjmp_buf, 1):
            // restore callee-saved registers and resume at the saved return address with v0=1.
            m_pendingCallbackRegisters = {};
            m_pendingCallbackRegisterMask.fill(false);
            m_pendingCallbackRegisters[REG_V0] = 1; // PSX-SPX: longjmp-style return value.
            m_pendingCallbackRegisterMask[REG_V0] = true;
            resumeAddress =
                readFromRegion<u32>(m_ram.data(), descriptorPhysical + 0x00u, MemoryMap::RAM_SIZE);
            m_pendingCallbackRegisters[REG_RA] = resumeAddress;
            m_pendingCallbackRegisterMask[REG_RA] = true;
            m_pendingCallbackRegisters[REG_SP] =
                readFromRegion<u32>(m_ram.data(), descriptorPhysical + 0x04u, MemoryMap::RAM_SIZE);
            m_pendingCallbackRegisterMask[REG_SP] = true;
            m_pendingCallbackRegisters[REG_FP] =
                readFromRegion<u32>(m_ram.data(), descriptorPhysical + 0x08u, MemoryMap::RAM_SIZE);
            m_pendingCallbackRegisterMask[REG_FP] = true;
            for (size_t reg = REG_S0; reg <= REG_S7; ++reg)
            {
                const Address offset = static_cast<Address>(0x0Cu + (reg - REG_S0) * sizeof(u32));
                m_pendingCallbackRegisters[reg] = readFromRegion<u32>(
                    m_ram.data(), descriptorPhysical + offset, MemoryMap::RAM_SIZE);
                m_pendingCallbackRegisterMask[reg] = true;
            }
            m_pendingCallbackRegisters[REG_GP] =
                readFromRegion<u32>(m_ram.data(), descriptorPhysical + 0x2Cu, MemoryMap::RAM_SIZE);
            m_pendingCallbackRegisterMask[REG_GP] = true;
            m_hasPendingCallbackRegisters = true;
        }

        if (resumeAddress != 0)
        {
            (void)invokeCallbackRaw(resumeAddress);
        }
    }
    catch (...)
    {
        m_hasPendingCallbackRegisters = false;
        m_pendingCallbackRegisterMask.fill(false);
        m_inHookEntryIntHandler = false;
        throw;
    }
    m_hasPendingCallbackRegisters = false;
    m_pendingCallbackRegisterMask.fill(false);
    m_inHookEntryIntHandler = false;
}

bool PsxSystem::consumePendingCallbackRegisters(std::array<u32, 32>& regsInOut)
{
    if (!m_hasPendingCallbackRegisters)
    {
        return false;
    }

    for (size_t reg = 0; reg < m_pendingCallbackRegisters.size(); ++reg)
    {
        if (m_pendingCallbackRegisterMask[reg])
        {
            regsInOut[reg] = m_pendingCallbackRegisters[reg];
        }
    }
    m_hasPendingCallbackRegisters = false;
    m_pendingCallbackRegisterMask.fill(false);
    return true;
}

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
