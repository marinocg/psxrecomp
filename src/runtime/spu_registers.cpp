#include "psxrecomp/runtime/spu.h"

#include <algorithm>
#include <cmath>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u16 kControlStatusModeMask = 0x003Fu;
constexpr u16 kTransferModeMask = 0x0030u;
constexpr u16 kTransferModeManualWrite = 0x0010u;
constexpr u16 kTransferModeDmaWrite = 0x0020u;
constexpr u16 kTransferModeDmaRead = 0x0030u;
constexpr u16 kStatusDmaRequest = 1u << 7;
constexpr u16 kStatusDmaWriteRequest = 1u << 8;
constexpr u16 kStatusDmaReadRequest = 1u << 9;
constexpr u16 kStatusBusy = 1u << 10;
constexpr u16 kControlIrqEnable = 1u << 6;
constexpr u16 kControlSpuEnable = 1u << 15;
constexpr u32 kControlApplyDelayCycles = 0x300u;
constexpr u32 kDmaReadWarmupCycles = 0x300u;
constexpr u32 kTransferBusyCycles = 0x300u;
constexpr u32 kTransferAddressScale = 8u;
constexpr u32 kSpuRamBytes = static_cast<u32>(Spu::RamWordCount * sizeof(u32));
} // namespace

size_t Spu::registerIndex(u32 offset)
{
    return (canonicalRegisterOffset(offset) / 2) % 0x100;
}

u16 Spu::readRegister(u32 offset) const
{
    const u32 canonical = canonicalRegisterOffset(offset);
    if (canonical < VoiceCount * 0x10u && (canonical % 0x10u) == 0x0Cu)
    {
        const Voice& voice = m_voices[static_cast<size_t>(canonical / 0x10u)];
        const float clampedEnvelope = std::clamp(voice.envelopeLevel, 0.0f, 1.0f);
        return static_cast<u16>(std::lround(clampedEnvelope * 32767.0f));
    }
    if (canonical == canonicalRegisterOffset(RegisterMap::EndxLow))
    {
        return buildEndxRegister(false);
    }
    if (canonical == canonicalRegisterOffset(RegisterMap::EndxHigh))
    {
        return buildEndxRegister(true);
    }
    if (canonical == canonicalRegisterOffset(RegisterMap::Status))
    {
        return buildStatusRegister();
    }
    if (canonical == canonicalRegisterOffset(RegisterMap::CurrentMainVolumeLeft))
    {
        return m_registers[registerIndex(RegisterMap::MainVolumeLeft)];
    }
    if (canonical == canonicalRegisterOffset(RegisterMap::CurrentMainVolumeRight))
    {
        return m_registers[registerIndex(RegisterMap::MainVolumeRight)];
    }
    return m_registers[registerIndex(canonical)];
}

bool Spu::hasIrqRequest() const
{
    return m_irqFlag && irqControlEnabled();
}

void Spu::writeRegister(u32 offset, u16 value)
{
    const u32 canonical = canonicalRegisterOffset(offset);
    if (canonical == canonicalRegisterOffset(RegisterMap::EndxLow) ||
        canonical == canonicalRegisterOffset(RegisterMap::EndxHigh))
    {
        return;
    }
    if (canonical == canonicalRegisterOffset(RegisterMap::Status))
    {
        return;
    }

    m_registers[registerIndex(canonical)] = value;
    if (canonical == canonicalRegisterOffset(RegisterMap::Control))
    {
        if ((value & (kControlIrqEnable | kControlSpuEnable)) !=
            (kControlIrqEnable | kControlSpuEnable))
        {
            m_irqFlag = false;
        }
        queueControlModeApply(value);
    }

    if (canonical < VoiceCount * 0x10u)
    {
        const size_t voiceIndex = static_cast<size_t>(canonical / 0x10u);
        const u32 voiceOffset = canonical % 0x10u;
        onVoiceRegisterWrite(voiceIndex, voiceOffset, value);
        return;
    }

    onGlobalRegisterWrite(canonical, value);
}

u16 Spu::buildEndxRegister(bool high) const
{
    const size_t baseVoice = high ? 16 : 0;
    u16 mask = 0;
    for (size_t bit = 0; bit < 16 && (baseVoice + bit) < VoiceCount; ++bit)
    {
        if (m_voices[baseVoice + bit].endx)
        {
            mask |= static_cast<u16>(1u << bit);
        }
    }
    return mask;
}

bool Spu::irqControlEnabled() const
{
    const u16 control = m_registers[registerIndex(RegisterMap::Control)];
    return (control & (kControlIrqEnable | kControlSpuEnable)) ==
           (kControlIrqEnable | kControlSpuEnable);
}

u16 Spu::buildStatusRegister() const
{
    const u16 transferMode = m_appliedControlBits & kTransferModeMask;
    const bool dmaWriteRequest = transferMode == kTransferModeDmaWrite;
    const bool dmaReadRequest = transferMode == kTransferModeDmaRead &&
                                m_dmaReadRequestDelayCyclesRemaining == 0 &&
                                m_controlApplyCyclesRemaining == 0;
    const bool busy = m_controlApplyCyclesRemaining != 0 ||
                      m_dmaReadRequestDelayCyclesRemaining != 0 || m_busyCyclesRemaining != 0;

    u16 status = m_appliedControlBits & kControlStatusModeMask;
    if (m_irqFlag)
    {
        status |= 1u << 6;
    }
    if (dmaWriteRequest || dmaReadRequest)
    {
        status |= kStatusDmaRequest;
    }
    if (dmaWriteRequest)
    {
        status |= kStatusDmaWriteRequest;
    }
    if (dmaReadRequest)
    {
        status |= kStatusDmaReadRequest;
    }
    if (busy)
    {
        status |= kStatusBusy;
    }
    return status;
}

void Spu::queueControlModeApply(u16 value)
{
    const u16 requestedBits = value & kControlStatusModeMask;
    m_pendingControlBits = requestedBits;
    m_controlApplyCyclesRemaining = kControlApplyDelayCycles;
}

void Spu::tickControlState(u32 cycles)
{
    auto tickDown = [cycles](u32& remaining)
    {
        if (remaining == 0)
        {
            return false;
        }
        if (cycles >= remaining)
        {
            remaining = 0;
            return true;
        }
        remaining -= cycles;
        return false;
    };

    const bool appliedControlThisTick = tickDown(m_controlApplyCyclesRemaining);
    if (appliedControlThisTick)
    {
        m_appliedControlBits = m_pendingControlBits;
        const u16 transferMode = m_appliedControlBits & kTransferModeMask;
        m_dmaReadRequestDelayCyclesRemaining =
            transferMode == kTransferModeDmaRead ? kDmaReadWarmupCycles : 0;
        runManualWriteTransfer();
    }

    if (!appliedControlThisTick)
    {
        tickDown(m_dmaReadRequestDelayCyclesRemaining);
    }
    tickDown(m_busyCyclesRemaining);
}

void Spu::noteDmaTransfer(bool fromRam, u32 wordCount)
{
    const u16 transferMode = m_appliedControlBits & kTransferModeMask;
    const bool transferModeMatches = (fromRam && transferMode == kTransferModeDmaWrite) ||
                                     (!fromRam && transferMode == kTransferModeDmaRead);
    if (!transferModeMatches || wordCount == 0)
    {
        return;
    }

    const u32 scaledBusyCycles = kTransferBusyCycles + std::min(wordCount, 0x100u) * 8u;
    m_busyCyclesRemaining = std::max(m_busyCyclesRemaining, scaledBusyCycles);
}

void Spu::onGlobalRegisterWrite(u32 offset, u16 value)
{
    switch (offset)
    {
    case canonicalRegisterOffset(RegisterMap::MainVolumeLeft):
        m_mixSettings.masterVolumeLeft = std::abs(normalizedSignedVolume(value));
        break;
    case canonicalRegisterOffset(RegisterMap::MainVolumeRight):
        m_mixSettings.masterVolumeRight = std::abs(normalizedSignedVolume(value));
        break;
    case canonicalRegisterOffset(RegisterMap::ReverbDepthLeft):
    case canonicalRegisterOffset(RegisterMap::ReverbDepthRight):
        m_mixSettings.reverbSend = std::abs(normalizedSignedVolume(value));
        break;
    case canonicalRegisterOffset(RegisterMap::KeyOnLow):
        applyVoiceMask(value, 0, true);
        break;
    case canonicalRegisterOffset(RegisterMap::KeyOnHigh):
        applyVoiceMask(0, value, true);
        break;
    case canonicalRegisterOffset(RegisterMap::KeyOffLow):
        applyVoiceMask(value, 0, false);
        break;
    case canonicalRegisterOffset(RegisterMap::KeyOffHigh):
        applyVoiceMask(0, value, false);
        break;
    case canonicalRegisterOffset(RegisterMap::PitchModulationLow):
    case canonicalRegisterOffset(RegisterMap::PitchModulationHigh):
    {
        const u32 mask =
            static_cast<u32>(m_registers[registerIndex(RegisterMap::PitchModulationLow)]) |
            (static_cast<u32>(m_registers[registerIndex(RegisterMap::PitchModulationHigh)]) << 16);
        applyPitchModulationMask(mask);
        break;
    }
    case canonicalRegisterOffset(RegisterMap::NoiseOnLow):
    case canonicalRegisterOffset(RegisterMap::NoiseOnHigh):
    {
        const u32 mask =
            static_cast<u32>(m_registers[registerIndex(RegisterMap::NoiseOnLow)]) |
            (static_cast<u32>(m_registers[registerIndex(RegisterMap::NoiseOnHigh)]) << 16);
        applyNoiseMask(mask);
        break;
    }
    case canonicalRegisterOffset(RegisterMap::ReverbOnLow):
    case canonicalRegisterOffset(RegisterMap::ReverbOnHigh):
    {
        const u32 mask =
            static_cast<u32>(m_registers[registerIndex(RegisterMap::ReverbOnLow)]) |
            (static_cast<u32>(m_registers[registerIndex(RegisterMap::ReverbOnHigh)]) << 16);
        applyReverbMask(mask);
        break;
    }
    case canonicalRegisterOffset(RegisterMap::RamTransferAddress):
        m_currentTransferAddress = (static_cast<u32>(value) * kTransferAddressScale) % kSpuRamBytes;
        break;
    case canonicalRegisterOffset(RegisterMap::RamTransferData):
        m_transferFifo.push_back(value);
        break;
    default:
        break;
    }
}

void Spu::onVoiceRegisterWrite(size_t voiceIndex, u32 voiceOffset, u16 value)
{
    Voice& voice = voiceAt(voiceIndex);
    switch (voiceOffset / 2)
    {
    case 0:
        voice.leftVolume = value;
        break;
    case 1:
        voice.rightVolume = value;
        break;
    case 2:
        voice.pitch = value;
        break;
    case 3:
        voice.startAddress = value;
        break;
    case 4:
        voice.adsrLo = value;
        break;
    case 5:
        voice.adsrHi = value;
        break;
    case 6:
        voice.envelopeLevel =
            std::clamp(static_cast<float>(static_cast<int16_t>(value)) / 32767.0f, 0.0f, 1.0f);
        break;
    case 7:
        voice.repeatAddress = value;
        voice.repeatAddressValid = true;
        break;
    default:
        break;
    }
}

} // namespace runtime
} // namespace psxrecomp
