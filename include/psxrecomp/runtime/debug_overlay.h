#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class RuntimeDebugOverlay
{
  public:
    void reset();
    void setLastFrameCycles(uint64_t cycles);
    void incrementDmaTransfers();
    void incrementInterruptsRaised();

    uint64_t dmaTransfers() const;
    uint64_t interruptsRaised() const;
    uint64_t frameCounter() const;
    uint64_t lastFrameCycles() const;
    uint32_t lastProgramCounter() const;
    uint32_t lastObservedProgramCounter() const;
    uint32_t lastArchitecturalProgramCounter() const;

    void setLastProgramCounter(uint32_t pc);
    void setLastObservedProgramCounter(uint32_t pc);
    void setLastArchitecturalProgramCounter(uint32_t pc);
    std::string renderText() const;
    void drawOnFrameBuffer(std::vector<uint16_t>& framebuffer, size_t width, size_t height) const;

  private:
    uint64_t m_frameCounter = 0;
    uint64_t m_lastFrameCycles = 0;
    uint64_t m_dmaTransfers = 0;
    uint64_t m_interruptsRaised = 0;
    uint32_t m_lastObservedProgramCounter = 0;
    uint32_t m_lastArchitecturalProgramCounter = 0;
};

} // namespace runtime
} // namespace psxrecomp
