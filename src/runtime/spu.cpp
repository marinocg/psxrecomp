#include "psxrecomp/runtime/spu.h"

namespace psxrecomp
{
namespace runtime
{

void Spu::reset()
{
    m_registers.fill(0);
    m_cycles = 0;
    m_lastDmaWord = 0;
}

u16 Spu::readRegister(u32 offset) const
{
    size_t index = (offset / 2) % m_registers.size();
    return m_registers[index];
}

void Spu::writeRegister(u32 offset, u16 value)
{
    size_t index = (offset / 2) % m_registers.size();
    m_registers[index] = value;
}

void Spu::tick(u32 cycles)
{
    m_cycles += cycles;
}

void Spu::writeDma(u32 value)
{
    m_lastDmaWord = value;
}

u32 Spu::cyclesElapsed() const
{
    return m_cycles;
}

u32 Spu::lastDmaWord() const
{
    return m_lastDmaWord;
}

} // namespace runtime
} // namespace psxrecomp
