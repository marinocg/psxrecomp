#include "psxrecomp/runtime/dma.h"

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr Address CHANNEL_BASE = 0x1F801080;
constexpr Address CHANNEL_STRIDE = 0x10;
constexpr Address CONTROL_REG = 0x1F8010F0;
constexpr Address INTERRUPT_REG = 0x1F8010F4;

constexpr u32 START_TRIGGER = 0x01000000;
} // namespace

void DmaController::reset()
{
    for (auto& channel : m_channels)
    {
        channel = {};
    }
    m_control = 0;
    m_interrupt = 0;
}

u32 DmaController::readRegister(Address address) const
{
    if (address == CONTROL_REG)
    {
        return m_control;
    }
    if (address == INTERRUPT_REG)
    {
        return m_interrupt;
    }

    auto port = channelFromAddress(address);
    if (!port)
    {
        return 0;
    }
    const auto& channel = m_channels[channelIndex(*port)];
    Address offset = address - (CHANNEL_BASE + channelIndex(*port) * CHANNEL_STRIDE);
    switch (offset)
    {
    case 0x0:
        return channel.baseAddress;
    case 0x4:
        return channel.blockControl;
    case 0x8:
        return channel.channelControl;
    default:
        return 0;
    }
}

std::optional<DmaPort> DmaController::writeRegister(Address address, u32 value)
{
    if (address == CONTROL_REG)
    {
        m_control = value;
        return std::nullopt;
    }
    if (address == INTERRUPT_REG)
    {
        m_interrupt = value;
        return std::nullopt;
    }

    auto port = channelFromAddress(address);
    if (!port)
    {
        return std::nullopt;
    }

    auto& channel = m_channels[channelIndex(*port)];
    Address offset = address - (CHANNEL_BASE + channelIndex(*port) * CHANNEL_STRIDE);
    switch (offset)
    {
    case 0x0:
        channel.baseAddress = value;
        break;
    case 0x4:
        channel.blockControl = value;
        break;
    case 0x8:
        channel.channelControl = value;
        if ((value & START_TRIGGER) != 0)
        {
            return port;
        }
        break;
    default:
        break;
    }

    return std::nullopt;
}

const DmaChannel& DmaController::channel(DmaPort port) const
{
    return m_channels[channelIndex(port)];
}

void DmaController::clearTrigger(DmaPort port)
{
    auto& channel = m_channels[channelIndex(port)];
    channel.channelControl &= ~START_TRIGGER;
}

size_t DmaController::channelIndex(DmaPort port) const
{
    return static_cast<size_t>(port);
}

std::optional<DmaPort> DmaController::channelFromAddress(Address address) const
{
    if (address < CHANNEL_BASE || address >= CHANNEL_BASE + CHANNEL_STRIDE * CHANNEL_COUNT)
    {
        return std::nullopt;
    }
    size_t index = (address - CHANNEL_BASE) / CHANNEL_STRIDE;
    return static_cast<DmaPort>(index);
}

} // namespace runtime
} // namespace psxrecomp
