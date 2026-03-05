#include "psxrecomp/runtime/psx_system.h"

#include <cstdlib>
#include <sstream>

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
} // namespace

void PsxSystem::invokeCallback(u32 address)
{
    if (address == 0)
    {
        return;
    }
    if (!m_callbackInvoker)
    {
        return;
    }

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

} // namespace runtime
} // namespace psxrecomp
