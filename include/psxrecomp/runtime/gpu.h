#pragma once

#include "psxrecomp/types.h"

#include <deque>

namespace psxrecomp
{
namespace runtime
{

class Gpu
{
  public:
    void reset();

    u32 readStatus() const;
    void writeStatus(u32 value);

    void writeCommand(u32 value);
    void writeDma(u32 value);

    size_t fifoDepth() const;
    u32 peekFifo() const;

  private:
    u32 m_status = 0;
    std::deque<u32> m_fifo;
    static constexpr size_t MAX_FIFO_DEPTH = 64;
};

} // namespace runtime
} // namespace psxrecomp
