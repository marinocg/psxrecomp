#pragma once

#include <cstdint>
#include <string>

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

    std::string renderText() const;

  private:
    uint64_t m_frameCounter = 0;
    uint64_t m_lastFrameCycles = 0;
    uint64_t m_dmaTransfers = 0;
    uint64_t m_interruptsRaised = 0;
};

} // namespace runtime
} // namespace psxrecomp
