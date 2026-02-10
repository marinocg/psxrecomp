#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <deque>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class Gpu
{
  public:
    static constexpr size_t VramWordCount = 512u * 1024u;

    void reset();

    u32 readStatus() const;
    u32 readData() const;
    void writeStatus(u32 value);

    void writeCommand(u32 value);
    void writeDma(u32 value);

    size_t fifoDepth() const;
    u32 peekFifo() const;

    const std::vector<u32>& vramWords() const;

  private:
    static constexpr u32 STATUS_READY = 0x14802000;
    static constexpr size_t MAX_FIFO_DEPTH = 64;

    void writeVramWord(u32 value);

    u32 m_status = 0;
    std::deque<u32> m_fifo;
    std::vector<u32> m_vram;
    size_t m_vramWriteCursor = 0;
};

} // namespace runtime
} // namespace psxrecomp
