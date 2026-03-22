#include "psxrecomp/runtime/psx_system.h"

namespace psxrecomp
{
namespace runtime
{

void PsxSystem::observeProgramCounter(Address pc, const u32* regs, size_t regCount)
{
    observeProgramCounter(pc, pc, regs, regCount);
}

void PsxSystem::observeProgramCounter(Address architecturalPc, Address observedPc, const u32* regs,
                                      size_t regCount)
{
    m_debugOverlay.setLastArchitecturalProgramCounter(architecturalPc);
    m_debugOverlay.setLastObservedProgramCounter(observedPc);
    noteExecutableEntry(architecturalPc);
    m_stallClassifier.recordPc(observedPc);
    m_diagRev2DecoderHandoffTracker.observePc(architecturalPc, regs, regCount, *this, &m_logger);
    m_diagTracepoints.observePc(architecturalPc, m_lastResumeAddress,
                                callbackContextCommitGeneration(), m_interrupts.readStatus(),
                                m_interrupts.readMask(), regs, regCount, this, &m_logger);
}

} // namespace runtime
} // namespace psxrecomp
