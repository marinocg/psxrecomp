#pragma once

#include "psxrecomp/types.h"

#include <array>
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
    u8 readData();
    u8 readInterruptFlags() const;
    u8 readInterruptEnable() const;

    void writeCommand(u8 value);
    void writeParam(u8 value);
    void writeInterruptFlags(u8 value);
    void writeInterruptEnable(u8 value);

    void writeDma(u32 value);
    u32 lastDmaWord() const;

  private:
    static constexpr size_t MAX_PARAMS = 16;
    static constexpr size_t RESPONSE_CAPACITY = 32;

    u8 m_status = 0;
    std::deque<u8> m_params;
    std::deque<u8> m_responses;
    u8 m_interruptFlags = 0;
    u8 m_interruptEnable = 0;
    u32 m_lastDmaWord = 0;
};

} // namespace runtime
} // namespace psxrecomp
