#include "psxrecomp/runtime/psx_system.h"

namespace psxrecomp
{
namespace runtime
{

void PsxSystem::observeProgramCounter(Address pc, const u32* regs, size_t regCount)
{
    m_debugOverlay.setLastProgramCounter(pc);
    m_stallClassifier.recordPc(pc);
    m_diagRev2DecoderHandoffTracker.observePc(pc, regs, regCount, *this, &m_logger);
    m_diagTracepoints.observePc(pc, m_lastResumeAddress, callbackContextCommitGeneration(),
                                m_interrupts.readStatus(), m_interrupts.readMask(), regs, regCount,
                                this, &m_logger);
}

} // namespace runtime
} // namespace psxrecomp
