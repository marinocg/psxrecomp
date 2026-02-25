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
    static constexpr size_t ChannelCount = 7;
    static constexpr Address ChannelBase = 0x1F801080;
    static constexpr Address ChannelStride = 0x10;
    static constexpr Address ControlReg = 0x1F8010F0;
    static constexpr Address InterruptReg = 0x1F8010F4;

    void reset();

    u32 readRegister(Address address) const;
    std::optional<DmaPort> writeRegister(Address address, u32 value);

    const DmaChannel& channel(DmaPort port) const;
    void clearTrigger(DmaPort port);
    void notifyTransferComplete(DmaPort port);
    bool irqRequested() const;

  private:
    DmaChannel m_channels[ChannelCount] = {};
    u32 m_control = 0;
    u32 m_interrupt = 0;

    size_t channelIndex(DmaPort port) const;
    std::optional<DmaPort> channelFromAddress(Address address) const;
};

} // namespace runtime
} // namespace psxrecomp
