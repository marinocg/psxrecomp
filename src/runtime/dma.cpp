#include "psxrecomp/runtime/dma.h"

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u32 START_TRIGGER = 0x01000000;
constexpr u32 DICR_FORCE_IRQ = 0x00008000u;
constexpr u32 DICR_CHANNEL_ENABLE_MASK = 0x007F0000u;
constexpr u32 DICR_MASTER_ENABLE = 0x00800000u;
constexpr u32 DICR_CHANNEL_FLAG_MASK = 0x7F000000u;
constexpr u32 DICR_MASTER_FLAG = 0x80000000u;

void updateMasterFlag(u32& dicr)
{
    const bool forceIrq = (dicr & DICR_FORCE_IRQ) != 0;
    const bool masterEnabled = (dicr & DICR_MASTER_ENABLE) != 0;
    const u32 enabledChannels = (dicr & DICR_CHANNEL_ENABLE_MASK) >> 16;
    const u32 flaggedChannels = (dicr & DICR_CHANNEL_FLAG_MASK) >> 24;
    const bool enabledFlagged = (enabledChannels & flaggedChannels) != 0;
    const bool irqRequested = forceIrq || (masterEnabled && enabledFlagged);
    if (irqRequested)
    {
        dicr |= DICR_MASTER_FLAG;
    }
    else
    {
        dicr &= ~DICR_MASTER_FLAG;
    }
}
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
    if (address == ControlReg)
    {
        return m_control;
    }
    if (address == InterruptReg)
    {
        return m_interrupt;
    }

    auto port = channelFromAddress(address);
    if (!port)
    {
        return 0;
    }
    const auto& channel = m_channels[channelIndex(*port)];
    Address offset =
        address - (ChannelBase + channelIndex(*port) * static_cast<Address>(ChannelStride));
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
    if (address == ControlReg)
    {
        m_control = value;
        return std::nullopt;
    }
    if (address == InterruptReg)
    {
        // DICR lower 24 bits are control fields; bits 24-30 are write-1-to-clear
        // channel flags; bit 31 is derived from force/master/flags state.
        const u32 clearFlags = (value >> 24) & 0x7Fu;
        m_interrupt = (m_interrupt & DICR_CHANNEL_FLAG_MASK) | (value & 0x00FFFFFFu);
        m_interrupt &= ~(clearFlags << 24);
        updateMasterFlag(m_interrupt);
        return std::nullopt;
    }

    auto port = channelFromAddress(address);
    if (!port)
    {
        return std::nullopt;
    }

    auto& channel = m_channels[channelIndex(*port)];
    Address offset =
        address - (ChannelBase + channelIndex(*port) * static_cast<Address>(ChannelStride));
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

void DmaController::notifyTransferComplete(DmaPort port)
{
    const u32 channelBit = 1u << (24u + static_cast<u32>(port));
    m_interrupt |= channelBit;
    updateMasterFlag(m_interrupt);
}

bool DmaController::irqRequested() const
{
    return (m_interrupt & DICR_MASTER_FLAG) != 0;
}

size_t DmaController::channelIndex(DmaPort port) const
{
    return static_cast<size_t>(port);
}

std::optional<DmaPort> DmaController::channelFromAddress(Address address) const
{
    if (address < ChannelBase ||
        address >= ChannelBase + ChannelStride * static_cast<Address>(ChannelCount))
    {
        return std::nullopt;
    }
    size_t index = (address - ChannelBase) / ChannelStride;
    return static_cast<DmaPort>(index);
}

} // namespace runtime
} // namespace psxrecomp
