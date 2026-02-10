#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class Spu
{
  public:
    static constexpr size_t RamWordCount = 128u * 1024u / sizeof(u32);

    void reset();

    u16 readRegister(u32 offset) const;
    void writeRegister(u32 offset, u16 value);

    void tick(u32 cycles);
    void writeDma(u32 value);

    u32 cyclesElapsed() const;
    u32 lastDmaWord() const;

    const std::vector<u32>& ramWords() const;

  private:
    std::array<u16, 0x100> m_registers{};
    u32 m_cycles = 0;
    u32 m_lastDmaWord = 0;
    std::vector<u32> m_ram;
    size_t m_ramWriteCursor = 0;
};

} // namespace runtime
} // namespace psxrecomp
