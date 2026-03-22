#include "psxrecomp/runtime/psx_system.h"

#include "bios_helpers.h"

#include <cstdlib>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
bool traceBiosFlowEnabled()
{
    if (const char* env = std::getenv("PSXRECOMP_TRACE_BIOS_FLOW"))
    {
        return env[0] == '1';
    }
    if (const char* env = std::getenv("PSXRECOMP_TRACE_BIOS"))
    {
        return env[0] == '1';
    }
    return false;
}
} // namespace

void PsxSystem::callBiosVector(u32 vector, u32* regs, size_t regCount)
{
    noteExecutableEntry();

    if (regs == nullptr || regCount < 32)
    {
        m_logger.log(LogLevel::Warn, "bios", "BIOS vector call with insufficient register file");
        return;
    }

    // Function number is in $t1 (register 9).
    const u32 functionId = regs[9];
    // Arguments are in $a0-$a3 (registers 4-7).
    const u32 a0 = regs[4];
    const u32 a1 = regs[5];
    const u32 a2 = regs[6];
    const u32 a3 = regs[7];

    const char* vecName = vectorName(vector);
    const bool traceBiosFlow = traceBiosFlowEnabled();

    m_stallClassifier.recordBiosCall(vector, functionId, a0);

    if (traceBiosFlow)
    {
        std::ostringstream trace;
        trace << "event=bios_call vector=" << vecName << " function=0x" << std::hex << functionId
              << " a0=0x" << a0 << " a1=0x" << a1 << " a2=0x" << a2 << " a3=0x" << a3 << " pc=0x"
              << m_debugOverlay.lastProgramCounter();
        m_logger.log(LogLevel::Info, "bios_trace", trace.str());
    }

    bool handled = false;
    if (vector == 0xA0)
    {
        handled = callBiosVectorA0(functionId, regs);
    }
    else if (vector == 0xB0)
    {
        handled = callBiosVectorB0(functionId, regs);
    }
    else if (vector == 0xC0)
    {
        handled = callBiosVectorC0(functionId, regs);
    }

    if (!handled)
    {
        std::ostringstream stream;
        stream << "Unhandled BIOS vector " << vecName << "(0x" << std::hex << functionId << ")"
               << " a0=0x" << a0 << " a1=0x" << a1;
        m_logger.log(LogLevel::Warn, "bios", stream.str());
    }
    if (traceBiosFlow)
    {
        std::ostringstream trace;
        trace << "event=bios_return vector=" << vecName << " function=0x" << std::hex << functionId
              << " handled=" << (handled ? 1 : 0) << " v0=0x" << regs[2] << " pc=0x"
              << m_debugOverlay.lastProgramCounter();
        m_logger.log(LogLevel::Info, "bios_trace", trace.str());
    }
}

} // namespace runtime
} // namespace psxrecomp
