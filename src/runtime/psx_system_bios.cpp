#include "psxrecomp/runtime/psx_system.h"

#include "bios_helpers.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

void PsxSystem::callBiosVector(u32 vector, u32* regs, size_t regCount)
{
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

    const char* vecName = vectorName(vector);

    if (const char* traceBios = std::getenv("PSXRECOMP_TRACE_BIOS"))
    {
        if (traceBios[0] == '1')
        {
            std::ostringstream trace;
            trace << "BIOS " << vecName << "(0x" << std::hex << functionId << ")" << " a0=0x" << a0
                  << " a1=0x" << a1 << " a2=0x" << a2 << " pc=0x"
                  << m_debugOverlay.lastProgramCounter();
            m_logger.log(LogLevel::Info, "bios_trace", trace.str());
        }
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
}

} // namespace runtime
} // namespace psxrecomp
