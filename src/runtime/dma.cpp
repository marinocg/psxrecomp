#include "psxrecomp/runtime/dma.h"

namespace psxrecomp
{
namespace runtime
{

namespace
{
// CHCR bit 24: Start/Busy - software sets to 1 to start; hardware clears when complete.
constexpr u32 CHCR_BUSY = 0x01000000u;
// CHCR bit 28: Start/Trigger - software sets to request start; hardware clears when begun.
constexpr u32 CHCR_START_TRIGGER = 0x10000000u;
// Legacy alias used by detection logic.
constexpr u32 START_TRIGGER = CHCR_BUSY;
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
    // PSX-SPX reset value is 0x07654321 (all enables=0, priorities 1-7).
    // The real BIOS enables all channels during startup. Since the synthetic
    // BIOS does not write DPCR, initialise to the post-BIOS state so that
    // game DMA transfers are not silently gated by the enable bits.
    m_control = 0x0FEDCBA9u; // 0x07654321 | 0x08888888 — all channels enabled
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
        // Trigger a transfer when bit 24 (Start/Busy) is set and the
        // DPCR master-enable for this channel is active.
        // PSX-SPX SyncMode semantics:
        //   SyncMode=0 (manual)      — requires bit 28 (Start/Trigger) too.
        //   SyncMode=1 (request)     — bit 24 alone is sufficient; no bit 28 needed.
        //   SyncMode=2 (linked-list) — bit 24 alone is sufficient; do NOT require bit 28.
        //   SyncMode=3               — reject safely; do not start regardless of bit 28.
        if ((value & CHCR_BUSY) != 0 && channelEnabled(*port))
        {
            const u32 syncMode = (value >> 9u) & 3u;
            if ((syncMode == 1u || syncMode == 2u) ||
                (syncMode == 0u && (value & CHCR_START_TRIGGER) != 0))
            {
                return port;
            }
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

void DmaController::clearStartTrigger(DmaPort port)
{
    // Bit 28 (Start/Trigger) clears when the transfer begins.
    m_channels[channelIndex(port)].channelControl &= ~CHCR_START_TRIGGER;
}

void DmaController::clearBusy(DmaPort port)
{
    // Bit 24 (Start/Busy) clears when the transfer completes.
    m_channels[channelIndex(port)].channelControl &= ~CHCR_BUSY;
}

void DmaController::clearTrigger(DmaPort port)
{
    // Legacy helper: clear both busy and start-trigger bits.
    auto& ch = m_channels[channelIndex(port)];
    ch.channelControl &= ~(CHCR_BUSY | CHCR_START_TRIGGER);
}

void DmaController::notifyTransferComplete(DmaPort port)
{
    // PSX-SPX: per-channel completion flag latches only when the
    // corresponding enable bit (DICR[16+n]) is set.
    const u32 n = static_cast<u32>(port);
    const u32 enableBit = 1u << (16u + n);
    if ((m_interrupt & enableBit) != 0u)
    {
        const u32 flagBit = 1u << (24u + n);
        m_interrupt |= flagBit;
    }
    updateMasterFlag(m_interrupt);
}

bool DmaController::channelEnabled(DmaPort port) const
{
    // DPCR layout: 4 bits per channel; bit 3 of each nibble is master enable.
    // Channel N enable bit = DPCR[3 + 4*N].
    const u32 shift = 3u + 4u * static_cast<u32>(port);
    return ((m_control >> shift) & 1u) != 0u;
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
