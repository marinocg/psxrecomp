#pragma once

#include "psxrecomp/types.h"

#include <optional>

namespace psxrecomp
{
namespace runtime
{

enum class DmaPort : u8
{
    MdecIn = 0,
    MdecOut = 1,
    Gpu = 2,
    Cdrom = 3,
    Spu = 4,
    Pio = 5,
    Otc = 6
};

struct DmaChannel
{
    u32 baseAddress = 0;
    u32 blockControl = 0;
    u32 channelControl = 0;
};

class DmaController
{
  public:
    void reset();

    u32 readRegister(Address address) const;
    std::optional<DmaPort> writeRegister(Address address, u32 value);

    const DmaChannel& channel(DmaPort port) const;
    void clearTrigger(DmaPort port);

  private:
    static constexpr size_t CHANNEL_COUNT = 7;
    DmaChannel m_channels[CHANNEL_COUNT] = {};
    u32 m_control = 0;
    u32 m_interrupt = 0;

    size_t channelIndex(DmaPort port) const;
    std::optional<DmaPort> channelFromAddress(Address address) const;
};

} // namespace runtime
} // namespace psxrecomp
