#include "psxrecomp/runtime/psx_system.h"

#include <cstdlib>
#include <sstream>
#include <utility>

namespace psxrecomp
{
namespace runtime
{

namespace
{
bool traceIrqFlowEnabled()
{
    if (const char* env = std::getenv("PSXRECOMP_TRACE_IRQ_FLOW"))
    {
        return env[0] == '1';
    }
    return false;
}

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
} // namespace

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
    if (pendingMasked != 0 && m_criticalSectionDepth == 0 && !m_inCallbackInvocation)
    {
        m_cop0.exceptionEnter(Cop0::ExceptionCode::Interrupt, m_debugOverlay.lastProgramCounter(),
                              false);
    }
    if (traceIrqFlowEnabled())
    {
        std::ostringstream msg;
        msg << "event=service_interrupts pending_masked=0x" << std::hex << pendingMasked
            << " status=0x" << m_interrupts.readStatus() << " mask=0x" << m_interrupts.readMask()
            << " critical_depth=" << std::dec << m_criticalSectionDepth
            << " in_callback=" << (m_inCallbackInvocation ? 1 : 0);
        m_logger.log(LogLevel::Info, "irq_trace", msg.str());
    }
    if (pendingMasked == 0)
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
            return;
        }
        return;
    }
    // HookEntryInt descriptor callback runs while IRQ status bits are visible.
    if (pendingMasked != 0 && m_criticalSectionDepth == 0 &&
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
            return;
        }
    }

    // The hook callback may acknowledge IRQ bits. Recompute pending state
    // before running priority chains so we don't act on stale masks.
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
        return;
    }

    // Kernel event delivery (OpenEvent/EnableEvent model).
    // A callback may execute ReturnFromException to abort further handling.
    try
    {
        m_dispatcher.serviceInterrupts(m_interrupts, m_events, m_criticalSectionDepth, &m_logger);
    }
    catch (const ReturnFromExceptionSignal&)
    {
        if (traceIrqFlowEnabled())
        {
            m_logger.log(LogLevel::Info, "irq_trace",
                         "event=return_from_exception source=dispatcher_pending");
        }
        return;
    }
}

u32 PsxSystem::resolveHookEntryIntCallback(u32 descriptorAddress) const
{
    if (descriptorAddress == 0)
    {
        return 0;
    }

    const Address descriptorPhysical = normalizeAddress(descriptorAddress);
    if (descriptorPhysical > MemoryMap::RAM_SIZE - sizeof(u32) || (descriptorPhysical & 0x3u) != 0u)
    {
        return 0;
    }

    const u32 callbackAddress =
        readFromRegion<u32>(m_ram.data(), descriptorPhysical, MemoryMap::RAM_SIZE);
    const Address callbackPhysical = normalizeAddress(callbackAddress);
    if ((callbackAddress & 0xE0000000u) != 0x80000000u ||
        callbackPhysical > MemoryMap::RAM_SIZE - sizeof(u32) || (callbackPhysical & 0x3u) != 0u)
    {
        return 0;
    }
    return callbackAddress;
}

void PsxSystem::invokeHookEntryIntHandler()
{
    m_inHookEntryIntHandler = true;
    try
    {
        const u32 callbackAddress = resolveHookEntryIntCallback(m_hookEntryInt.descriptorAddress);
        if (callbackAddress != 0)
        {
            // HookEntryInt is implemented by BIOS as longjmp(setjmp_buf, 1):
            // restore callee-saved registers and resume at saved RA with v0=1.
            const Address descriptorPhysical = normalizeAddress(m_hookEntryInt.descriptorAddress);
            if (descriptorPhysical <= MemoryMap::RAM_SIZE - 0x30u)
            {
                m_pendingCallbackRegisters = {};
                m_pendingCallbackRegisterMask.fill(false);
                m_pendingCallbackRegisters[REG_V0] = 1; // PSX-SPX: hook callback enters with r2=1.
                m_pendingCallbackRegisterMask[REG_V0] = true;
                m_pendingCallbackRegisters[REG_RA] = readFromRegion<u32>(
                    m_ram.data(), descriptorPhysical + 0x00u, MemoryMap::RAM_SIZE);
                m_pendingCallbackRegisterMask[REG_RA] = true;
                m_pendingCallbackRegisters[REG_SP] = readFromRegion<u32>(
                    m_ram.data(), descriptorPhysical + 0x04u, MemoryMap::RAM_SIZE);
                m_pendingCallbackRegisterMask[REG_SP] = true;
                m_pendingCallbackRegisters[REG_FP] = readFromRegion<u32>(
                    m_ram.data(), descriptorPhysical + 0x08u, MemoryMap::RAM_SIZE);
                m_pendingCallbackRegisterMask[REG_FP] = true;
                for (size_t reg = REG_S0; reg <= REG_S7; ++reg)
                {
                    const Address offset =
                        static_cast<Address>(0x0Cu + (reg - REG_S0) * sizeof(u32));
                    m_pendingCallbackRegisters[reg] = readFromRegion<u32>(
                        m_ram.data(), descriptorPhysical + offset, MemoryMap::RAM_SIZE);
                    m_pendingCallbackRegisterMask[reg] = true;
                }
                m_pendingCallbackRegisters[REG_GP] = readFromRegion<u32>(
                    m_ram.data(), descriptorPhysical + 0x2Cu, MemoryMap::RAM_SIZE);
                m_pendingCallbackRegisterMask[REG_GP] = true;
                m_hasPendingCallbackRegisters = true;
            }
        }

        (void)invokeCallbackRaw(callbackAddress);
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

void PsxSystem::invokeCallback(u32 address)
{
    if (address == 0)
    {
        return;
    }
    if (m_callbackInvoker)
    {
        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=callback_invoke mode=void addr=0x" << std::hex << address << " pc=0x"
                << m_debugOverlay.lastProgramCounter();
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        const bool previousInCallbackInvocation = m_inCallbackInvocation;
        m_inCallbackInvocation = true;
        try
        {
            (void)m_callbackInvoker(address);
        }
        catch (const ReturnFromExceptionSignal&)
        {
            if (traceIrqFlowEnabled())
            {
                std::ostringstream msg;
                msg << "event=return_from_exception source=callback_invoke addr=0x" << std::hex
                    << address;
                m_logger.log(LogLevel::Info, "irq_trace", msg.str());
            }
            m_inCallbackInvocation = previousInCallbackInvocation;
            return;
        }
        catch (...)
        {
            m_inCallbackInvocation = previousInCallbackInvocation;
            throw;
        }
        m_inCallbackInvocation = previousInCallbackInvocation;
    }
}

u32 PsxSystem::invokeCallbackRaw(u32 address)
{
    if (address == 0)
    {
        return 0;
    }
    if (!m_callbackInvoker)
    {
        return 0;
    }

    const bool previousInCallbackInvocation = m_inCallbackInvocation;
    m_inCallbackInvocation = true;
    try
    {
        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=callback_invoke mode=raw addr=0x" << std::hex << address << " pc=0x"
                << m_debugOverlay.lastProgramCounter();
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        const u32 result = m_callbackInvoker(address);
        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=callback_return mode=raw addr=0x" << std::hex << address << " v0=0x"
                << result;
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        m_inCallbackInvocation = previousInCallbackInvocation;
        return result;
    }
    catch (...)
    {
        m_inCallbackInvocation = previousInCallbackInvocation;
        throw;
    }
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
                    << " index=" << safety << " node=0x" << std::hex << node << " func1=0x"
                    << func1 << " func2=0x" << func2;
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
