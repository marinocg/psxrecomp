#include "psxrecomp/runtime/gpu.h"

namespace psxrecomp
{
namespace runtime
{

void Gpu::reset()
{
    m_status = 0x14802000;
    m_fifo.clear();
}

u32 Gpu::readStatus() const
{
    return m_status;
}

void Gpu::writeStatus(u32 value)
{
    m_status = value;
}

void Gpu::writeCommand(u32 value)
{
    if (m_fifo.size() < MAX_FIFO_DEPTH)
    {
        m_fifo.push_back(value);
    }
}

void Gpu::writeDma(u32 value)
{
    writeCommand(value);
}

size_t Gpu::fifoDepth() const
{
    return m_fifo.size();
}

u32 Gpu::peekFifo() const
{
    if (m_fifo.empty())
    {
        return 0;
    }
    return m_fifo.front();
}

} // namespace runtime
} // namespace psxrecomp
