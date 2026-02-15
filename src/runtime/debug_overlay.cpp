#include "psxrecomp/runtime/debug_overlay.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

void RuntimeDebugOverlay::reset()
{
    m_frameCounter = 0;
    m_lastFrameCycles = 0;
    m_dmaTransfers = 0;
    m_interruptsRaised = 0;
    m_lastProgramCounter = 0;
}

void RuntimeDebugOverlay::setLastFrameCycles(uint64_t cycles)
{
    m_lastFrameCycles = cycles;
    ++m_frameCounter;
}

void RuntimeDebugOverlay::incrementDmaTransfers()
{
    ++m_dmaTransfers;
}

void RuntimeDebugOverlay::incrementInterruptsRaised()
{
    ++m_interruptsRaised;
}

uint64_t RuntimeDebugOverlay::dmaTransfers() const
{
    return m_dmaTransfers;
}

uint64_t RuntimeDebugOverlay::interruptsRaised() const
{
    return m_interruptsRaised;
}

uint64_t RuntimeDebugOverlay::frameCounter() const
{
    return m_frameCounter;
}

uint64_t RuntimeDebugOverlay::lastFrameCycles() const
{
    return m_lastFrameCycles;
}

uint32_t RuntimeDebugOverlay::lastProgramCounter() const
{
    return m_lastProgramCounter;
}

void RuntimeDebugOverlay::setLastProgramCounter(uint32_t pc)
{
    m_lastProgramCounter = pc;
}

std::string RuntimeDebugOverlay::renderText() const
{
    std::ostringstream stream;
    stream << "frame=" << m_frameCounter << " cycles=" << m_lastFrameCycles
           << " dma=" << m_dmaTransfers << " irq=" << m_interruptsRaised << " pc=0x" << std::hex
           << std::uppercase << m_lastProgramCounter;
    return stream.str();
}

} // namespace runtime
} // namespace psxrecomp
