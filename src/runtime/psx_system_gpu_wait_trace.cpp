#include "psxrecomp/runtime/psx_system.h"

namespace psxrecomp
{
namespace runtime
{

void PsxSystem::observeProgramCounter(Address pc)
{
    m_debugOverlay.setLastProgramCounter(pc);
    m_stallClassifier.recordPc(pc);
    m_diagTracepoints.observePc(pc, &m_logger);
}

} // namespace runtime
} // namespace psxrecomp