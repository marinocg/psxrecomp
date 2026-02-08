#pragma once

#include "psxrecomp/types.h"

#include <deque>

namespace psxrecomp
{
namespace runtime
{

class Cdrom
{
  public:
    void reset();

    u8 readStatus() const;
    void writeCommand(u8 value);
    void writeParam(u8 value);

    void writeDma(u32 value);
    u32 lastDmaWord() const;

  private:
    u8 m_status = 0;
    std::deque<u8> m_params;
    u32 m_lastDmaWord = 0;
    static constexpr size_t MAX_PARAMS = 16;
};

} // namespace runtime
} // namespace psxrecomp
