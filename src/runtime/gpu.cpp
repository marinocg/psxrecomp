#include "psxrecomp/runtime/gpu.h"

namespace psxrecomp
{
namespace runtime
{

void Gpu::reset()
{
    m_status = STATUS_READY;
    m_fifo.clear();
    m_vram.assign(VramWordCount, 0);
    m_vramWriteCursor = 0;
}

u32 Gpu::readStatus() const
{
    return m_status;
}

u32 Gpu::readData() const
{
    if (m_fifo.empty())
    {
        return 0;
    }
    return m_fifo.front();
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
    writeVramWord(value);
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

const std::vector<u32>& Gpu::vramWords() const
{
    return m_vram;
}

void Gpu::writeVramWord(u32 value)
{
    if (m_vram.empty())
    {
        return;
    }
    m_vram[m_vramWriteCursor] = value;
    m_vramWriteCursor = (m_vramWriteCursor + 1) % m_vram.size();
}

} // namespace runtime
} // namespace psxrecomp
