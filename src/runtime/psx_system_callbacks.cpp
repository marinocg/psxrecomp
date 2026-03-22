#include "psxrecomp/runtime/psx_system.h"

#include "irq_trace_utils.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

void PsxSystem::invokeCallback(u32 address, u32 descriptorAddress)
{
    noteExecutableEntry();

    if (address == 0)
    {
        return;
    }
    if (!m_callbackInvoker)
    {
        return;
    }

    const bool emitTraceLogs = traceIrqFlowEnabled();
    const Address returnSite = m_debugOverlay.lastProgramCounter();
    const u32 callbackGenerationBefore = m_callbackContextCommitGeneration;
    const u32 irqStatusBefore = m_interrupts.readStatus();
    const u32 irqMaskBefore = m_interrupts.readMask();
    const u8 cdHintStatusBefore = static_cast<u8>(m_cdrom.readInterruptFlags() & 0x1Fu);
    const u8 cdHintMaskBefore = static_cast<u8>(m_cdrom.readInterruptEnable() & 0x1Fu);
    const bool cop0InterruptEligibleBefore = m_cop0.shouldTakeInterruptException();
    m_callbackTrace.beginInvocation(
        address, descriptorAddress, returnSite, callbackGenerationBefore, irqStatusBefore,
        irqMaskBefore, cdHintStatusBefore, cdHintMaskBefore, cop0InterruptEligibleBefore);
    if (emitTraceLogs)
    {
        std::ostringstream msg;
        msg << "event=callback_invoke mode=void addr=0x" << std::hex << address << " descriptor=0x"
            << descriptorAddress << " pc=0x" << returnSite << " gen_before=" << std::dec
            << callbackGenerationBefore << std::hex << " irq_before=0x" << irqStatusBefore << "/0x"
            << irqMaskBefore << std::dec
            << " cop0_before=" << (cop0InterruptEligibleBefore ? 1 : 0);
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
        const Address exitPc = m_debugOverlay.lastProgramCounter();
        m_callbackTrace.finishInvocation(
            exitPc, true, m_callbackContextCommitGeneration, m_interrupts.readStatus(),
            m_interrupts.readMask(), static_cast<u8>(m_cdrom.readInterruptFlags() & 0x1Fu),
            static_cast<u8>(m_cdrom.readInterruptEnable() & 0x1Fu),
            m_cop0.shouldTakeInterruptException(), &m_logger, emitTraceLogs);
        if (emitTraceLogs)
        {
            std::ostringstream msg;
            msg << "event=return_from_exception source=callback_invoke addr=0x" << std::hex
                << address << " exit_pc=0x" << exitPc;
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        m_inCallbackInvocation = previousInCallbackInvocation;
        return;
    }
    catch (...)
    {
        m_callbackTrace.finishInvocation(
            m_debugOverlay.lastProgramCounter(), false, m_callbackContextCommitGeneration,
            m_interrupts.readStatus(), m_interrupts.readMask(),
            static_cast<u8>(m_cdrom.readInterruptFlags() & 0x1Fu),
            static_cast<u8>(m_cdrom.readInterruptEnable() & 0x1Fu),
            m_cop0.shouldTakeInterruptException(), &m_logger, emitTraceLogs);
        m_inCallbackInvocation = previousInCallbackInvocation;
        throw;
    }
    m_callbackTrace.finishInvocation(
        m_debugOverlay.lastProgramCounter(), false, m_callbackContextCommitGeneration,
        m_interrupts.readStatus(), m_interrupts.readMask(),
        static_cast<u8>(m_cdrom.readInterruptFlags() & 0x1Fu),
        static_cast<u8>(m_cdrom.readInterruptEnable() & 0x1Fu),
        m_cop0.shouldTakeInterruptException(), &m_logger, emitTraceLogs);
    m_inCallbackInvocation = previousInCallbackInvocation;
}

u32 PsxSystem::invokeCallbackRaw(u32 address, u32 descriptorAddress)
{
    noteExecutableEntry();

    if (address == 0)
    {
        return 0;
    }
    if (!m_callbackInvoker)
    {
        return 0;
    }

    const bool emitTraceLogs = traceIrqFlowEnabled();
    const Address returnSite = m_debugOverlay.lastProgramCounter();
    const u32 callbackGenerationBefore = m_callbackContextCommitGeneration;
    const u32 irqStatusBefore = m_interrupts.readStatus();
    const u32 irqMaskBefore = m_interrupts.readMask();
    const u8 cdHintStatusBefore = static_cast<u8>(m_cdrom.readInterruptFlags() & 0x1Fu);
    const u8 cdHintMaskBefore = static_cast<u8>(m_cdrom.readInterruptEnable() & 0x1Fu);
    const bool cop0InterruptEligibleBefore = m_cop0.shouldTakeInterruptException();
    m_callbackTrace.beginInvocation(
        address, descriptorAddress, returnSite, callbackGenerationBefore, irqStatusBefore,
        irqMaskBefore, cdHintStatusBefore, cdHintMaskBefore, cop0InterruptEligibleBefore);
    const bool previousInCallbackInvocation = m_inCallbackInvocation;
    m_inCallbackInvocation = true;
    try
    {
        if (emitTraceLogs)
        {
            std::ostringstream msg;
            msg << "event=callback_invoke mode=raw addr=0x" << std::hex << address
                << " descriptor=0x" << descriptorAddress << " pc=0x" << returnSite
                << " gen_before=" << std::dec << callbackGenerationBefore << std::hex
                << " irq_before=0x" << irqStatusBefore << "/0x" << irqMaskBefore << std::dec
                << " cop0_before=" << (cop0InterruptEligibleBefore ? 1 : 0);
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        const u32 result = m_callbackInvoker(address);
        m_callbackTrace.finishInvocation(
            m_debugOverlay.lastProgramCounter(), false, m_callbackContextCommitGeneration,
            m_interrupts.readStatus(), m_interrupts.readMask(),
            static_cast<u8>(m_cdrom.readInterruptFlags() & 0x1Fu),
            static_cast<u8>(m_cdrom.readInterruptEnable() & 0x1Fu),
            m_cop0.shouldTakeInterruptException(), &m_logger, emitTraceLogs);
        if (emitTraceLogs)
        {
            std::ostringstream msg;
            msg << "event=callback_return mode=raw addr=0x" << std::hex << address << " v0=0x"
                << result;
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        m_inCallbackInvocation = previousInCallbackInvocation;
        return result;
    }
    catch (const ReturnFromExceptionSignal&)
    {
        const Address exitPc = m_debugOverlay.lastProgramCounter();
        m_callbackTrace.finishInvocation(
            exitPc, true, m_callbackContextCommitGeneration, m_interrupts.readStatus(),
            m_interrupts.readMask(), static_cast<u8>(m_cdrom.readInterruptFlags() & 0x1Fu),
            static_cast<u8>(m_cdrom.readInterruptEnable() & 0x1Fu),
            m_cop0.shouldTakeInterruptException(), &m_logger, emitTraceLogs);
        m_inCallbackInvocation = previousInCallbackInvocation;
        throw;
    }
    catch (...)
    {
        m_callbackTrace.finishInvocation(
            m_debugOverlay.lastProgramCounter(), false, m_callbackContextCommitGeneration,
            m_interrupts.readStatus(), m_interrupts.readMask(),
            static_cast<u8>(m_cdrom.readInterruptFlags() & 0x1Fu),
            static_cast<u8>(m_cdrom.readInterruptEnable() & 0x1Fu),
            m_cop0.shouldTakeInterruptException(), &m_logger, emitTraceLogs);
        m_inCallbackInvocation = previousInCallbackInvocation;
        throw;
    }
}

} // namespace runtime
} // namespace psxrecomp
