#include "psxrecomp/runtime/spu.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u32 kVoiceRegisterStride = 0x10;
constexpr u16 kControlCdAudioEnable = 1u << 0;
constexpr u16 kControlExternalAudioEnable = 1u << 1;
constexpr u16 kControlCdAudioReverb = 1u << 2;
constexpr u16 kControlExternalAudioReverb = 1u << 3;
constexpr float kEnvelopeAttackStep = 0.03f;
constexpr float kEnvelopeDecayStep = 0.01f;
constexpr float kEnvelopeReleaseStep = 0.02f;
constexpr float kEnvelopeSustainLevel = 0.7f;
constexpr std::array<int, 5> kFilterK0 = {0, 60, 115, 98, 122};
constexpr std::array<int, 5> kFilterK1 = {0, 0, -52, -55, -60};

float clampUnit(float value)
{
    return std::clamp(value, -1.0f, 1.0f);
}

int16_t clampI16(int value)
{
    return static_cast<int16_t>(std::clamp(value, -32768, 32767));
}

u8 readByteFromRam(const std::vector<u32>& ramWords, size_t absoluteByte)
{
    const size_t wordIndex = (absoluteByte / sizeof(u32)) % ramWords.size();
    const size_t byteLane = absoluteByte % sizeof(u32);
    return static_cast<u8>((ramWords[wordIndex] >> (byteLane * 8)) & 0xFFu);
}
} // namespace

void SpuNullAudioBackend::submitSamples(const std::vector<int16_t>& interleavedStereoPcm)
{
    m_lastSubmittedBuffer = interleavedStereoPcm;
}

const std::vector<int16_t>& SpuNullAudioBackend::lastSubmittedBuffer() const
{
    return m_lastSubmittedBuffer;
}

Spu::Spu() : m_ram(RamWordCount, 0), m_reverbRing(SamplesPerTick * 2, 0)
{
    m_audioBackend = std::make_shared<SpuNullAudioBackend>();
    reset();
}

void Spu::reset()
{
    m_registers.fill(0);
    for (Voice& voice : m_voices)
    {
        voice = Voice{};
    }

    m_mixSettings = MixSettings{};
    m_cycles = 0;
    m_pendingCycles = 0;
    m_lastDmaWord = 0;
    m_appliedControlBits = 0;
    m_pendingControlBits = 0;
    m_controlApplyCyclesRemaining = 0;
    m_dmaReadRequestDelayCyclesRemaining = 0;
    m_busyCyclesRemaining = 0;
    m_irqFlag = false;
    std::fill(m_ram.begin(), m_ram.end(), 0);
    m_currentTransferAddress = 0;
    m_transferFifo.clear();

    m_mixedAudioBuffer.clear();
    std::fill(m_reverbRing.begin(), m_reverbRing.end(), 0);
    m_reverbIndex = 0;
    m_cdAudioRing.clear();
}

void Spu::primeBootState()
{
    // Simulate post-BIOS SPU state: transfer control normal (0x0004) and
    // control bits set to DMA-write mode (bits [5:4] = 0b10 = 0x0020) with
    // SPU enable (bit 15) and IRQ enable (bit 6) active. The BIOS sets these
    // during boot; apply them immediately without the normal cycle delay so
    // that DMA transfers work as soon as the runtime is initialized.
    m_registers[registerIndex(RegisterMap::TransferControl)] = 0x0004u;
    constexpr u16 kBootControlBits =
        0x8061u; // SpuEnable | IrqEnable | DmaWriteMode | CdAudioEnable
    m_registers[registerIndex(RegisterMap::Control)] = kBootControlBits;
    m_pendingControlBits = kBootControlBits & 0x003Fu;
    m_appliedControlBits = m_pendingControlBits;
    m_controlApplyCyclesRemaining = 0;
}

void Spu::tick(u32 cycles)
{
    m_cycles += cycles;
    m_pendingCycles += cycles;
    tickControlState(cycles);

    const u32 samplesToGenerate = m_pendingCycles / CyclesPerSample;
    if (samplesToGenerate == 0)
    {
        return;
    }

    m_pendingCycles -= samplesToGenerate * CyclesPerSample;
    m_mixedAudioBuffer.clear();
    m_mixedAudioBuffer.reserve(static_cast<size_t>(samplesToGenerate) * 2);

    for (u32 sample = 0; sample < samplesToGenerate; ++sample)
    {
        float leftMix = 0.0f;
        float rightMix = 0.0f;
        float reverbInput = 0.0f;
        float cdLeft = 0.0f;
        float cdRight = 0.0f;

        if (m_cdAudioRing.size() >= 2)
        {
            cdLeft = static_cast<float>(m_cdAudioRing.front()) / 32768.0f;
            m_cdAudioRing.pop_front();
            cdRight = static_cast<float>(m_cdAudioRing.front()) / 32768.0f;
            m_cdAudioRing.pop_front();
        }

        for (Voice& voice : m_voices)
        {
            if (!voice.isActive)
            {
                continue;
            }

            const int16_t pcm = nextVoiceSample(voice);
            const float sampleNorm = static_cast<float>(pcm) / 32768.0f;
            updateEnvelope(voice);

            const float leftVoice =
                sampleNorm * normalizedSignedVolume(voice.leftVolume) * voice.envelopeLevel;
            const float rightVoice =
                sampleNorm * normalizedSignedVolume(voice.rightVolume) * voice.envelopeLevel;

            leftMix += leftVoice;
            rightMix += rightVoice;

            if (voice.reverbEnabled)
            {
                reverbInput += (leftVoice + rightVoice) * 0.5f;
            }
        }

        if ((m_appliedControlBits & kControlCdAudioEnable) != 0u)
        {
            leftMix += cdLeft;
            rightMix += cdRight;
        }
        if ((m_appliedControlBits & kControlCdAudioReverb) != 0u)
        {
            reverbInput += (cdLeft + cdRight) * 0.5f;
        }
        if ((m_appliedControlBits & kControlExternalAudioEnable) != 0u)
        {
            // External audio input is not sourced yet, but the routing bit is modeled.
        }
        if ((m_appliedControlBits & kControlExternalAudioReverb) != 0u)
        {
            // External audio reverb routing is tracked for register fidelity.
        }

        reverbInput *= m_mixSettings.reverbSend;
        const float reverbOut = static_cast<float>(m_reverbRing[m_reverbIndex]) / 32768.0f;
        const float reverbWrite = clampUnit(reverbInput + reverbOut * m_mixSettings.reverbFeedback);
        m_reverbRing[m_reverbIndex] = static_cast<int16_t>(std::lround(reverbWrite * 32767.0f));
        m_reverbIndex = (m_reverbIndex + 1) % m_reverbRing.size();

        const float leftOut = clampUnit(leftMix * m_mixSettings.masterVolumeLeft + reverbOut);
        const float rightOut = clampUnit(rightMix * m_mixSettings.masterVolumeRight + reverbOut);

        m_mixedAudioBuffer.push_back(static_cast<int16_t>(std::lround(leftOut * 32767.0f)));
        m_mixedAudioBuffer.push_back(static_cast<int16_t>(std::lround(rightOut * 32767.0f)));
    }

    mixQueuedSamples();
}
u32 Spu::cyclesElapsed() const
{
    return m_cycles;
}

u32 Spu::lastDmaWord() const
{
    return m_lastDmaWord;
}

const std::vector<u32>& Spu::ramWords() const
{
    return m_ram;
}

const std::array<Spu::Voice, Spu::VoiceCount>& Spu::voices() const
{
    return m_voices;
}

const std::vector<int16_t>& Spu::mixedAudioBuffer() const
{
    return m_mixedAudioBuffer;
}

Spu::Voice& Spu::voiceAt(size_t voiceIndex)
{
    return m_voices[voiceIndex % VoiceCount];
}

void Spu::applyVoiceMask(u16 lowMask, u16 highMask, bool keyOn)
{
    const u32 mask = static_cast<u32>(lowMask) | (static_cast<u32>(highMask) << 16);
    for (size_t voiceIndex = 0; voiceIndex < VoiceCount; ++voiceIndex)
    {
        if ((mask & (1u << voiceIndex)) == 0)
        {
            continue;
        }

        Voice& voice = voiceAt(voiceIndex);
        if (keyOn)
        {
            voice.keyOn = true;
            voice.keyOff = false;
            voice.isActive = true;
            voice.endx = false;
            voice.blockLoopEnd = false;
            voice.blockLoopRepeat = false;
            voice.envelopePhase = Voice::EnvelopePhase::Attack;
            voice.envelopeLevel = 0.0f;
            voice.samplePosition = 0.0f;
            voice.currentAddress = voice.startAddress;
            voice.prevSample1 = 0;
            voice.prevSample2 = 0;
            voice.decodedSampleIndex = voice.decodedBlock.size();
            voice.blockLoaded = false;
        }
        else if (voice.isActive)
        {
            voice.keyOff = true;
            voice.keyOn = false;
            voice.envelopePhase = Voice::EnvelopePhase::Release;
        }
    }
}

void Spu::applyReverbMask(u32 voiceMask)
{
    for (size_t voiceIndex = 0; voiceIndex < VoiceCount; ++voiceIndex)
    {
        voiceAt(voiceIndex).reverbEnabled = (voiceMask & (1u << voiceIndex)) != 0;
    }
}

void Spu::applyPitchModulationMask(u32 voiceMask)
{
    for (size_t voiceIndex = 0; voiceIndex < VoiceCount; ++voiceIndex)
    {
        const bool enabled = voiceIndex != 0 && (voiceMask & (1u << voiceIndex)) != 0;
        voiceAt(voiceIndex).pitchModulationEnabled = enabled;
    }
}

void Spu::applyNoiseMask(u32 voiceMask)
{
    for (size_t voiceIndex = 0; voiceIndex < VoiceCount; ++voiceIndex)
    {
        voiceAt(voiceIndex).noiseEnabled = (voiceMask & (1u << voiceIndex)) != 0;
    }
}

void Spu::updateEnvelope(Voice& voice)
{
    switch (voice.envelopePhase)
    {
    case Voice::EnvelopePhase::Attack:
        voice.envelopeLevel = std::min(1.0f, voice.envelopeLevel + kEnvelopeAttackStep);
        if (voice.envelopeLevel >= 1.0f)
        {
            voice.envelopePhase = Voice::EnvelopePhase::Decay;
        }
        break;
    case Voice::EnvelopePhase::Decay:
        voice.envelopeLevel =
            std::max(kEnvelopeSustainLevel, voice.envelopeLevel - kEnvelopeDecayStep);
        if (voice.envelopeLevel <= kEnvelopeSustainLevel)
        {
            voice.envelopePhase = Voice::EnvelopePhase::Sustain;
        }
        break;
    case Voice::EnvelopePhase::Sustain:
        break;
    case Voice::EnvelopePhase::Release:
        voice.envelopeLevel = std::max(0.0f, voice.envelopeLevel - kEnvelopeReleaseStep);
        if (voice.envelopeLevel <= 0.0f)
        {
            voice.envelopePhase = Voice::EnvelopePhase::Off;
            voice.isActive = false;
        }
        break;
    case Voice::EnvelopePhase::Off:
        voice.isActive = false;
        voice.envelopeLevel = 0.0f;
        break;
    }
}

void Spu::finishVoiceBlock(Voice& voice)
{
    if (voice.blockLoopEnd)
    {
        voice.endx = true;
        voice.currentAddress = voice.repeatAddress;
        if (!voice.blockLoopRepeat)
        {
            voice.keyOn = false;
            voice.keyOff = true;
            voice.envelopePhase = Voice::EnvelopePhase::Release;
            voice.envelopeLevel = 0.0f;
        }
    }
    else
    {
        voice.currentAddress = static_cast<u16>(voice.currentAddress + 2);
    }

    voice.blockLoaded = false;
    voice.blockLoopEnd = false;
    voice.blockLoopRepeat = false;
}

int16_t Spu::nextVoiceSample(Voice& voice)
{
    if (!voice.blockLoaded || voice.decodedSampleIndex >= voice.decodedBlock.size())
    {
        loadAdpcmBlock(voice);
    }

    if (!voice.isActive)
    {
        return 0;
    }

    const int16_t sample = voice.decodedBlock[voice.decodedSampleIndex];
    const float pitchStep = std::max(0.25f, static_cast<float>(voice.pitch) / 4096.0f);
    voice.samplePosition += pitchStep;

    while (voice.samplePosition >= 1.0f)
    {
        voice.samplePosition -= 1.0f;
        ++voice.decodedSampleIndex;
        if (voice.decodedSampleIndex >= voice.decodedBlock.size())
        {
            finishVoiceBlock(voice);
            if (voice.isActive)
            {
                loadAdpcmBlock(voice);
            }
            break;
        }
    }

    return sample;
}

void Spu::loadAdpcmBlock(Voice& voice)
{
    const u32 byteAddress = normalizeRamByteAddress(static_cast<u32>(voice.currentAddress) * 8u);
    maybeTriggerIrqRange(byteAddress, 16u);

    const u8 header = readByteFromRam(m_ram, byteAddress + 0);
    const u8 flags = readByteFromRam(m_ram, byteAddress + 1);

    const int shift = header & 0x0F;
    const int filter = std::min(4, static_cast<int>((header >> 4) & 0x0F));

    for (size_t i = 0; i < voice.decodedBlock.size(); ++i)
    {
        const u8 packed = readByteFromRam(m_ram, byteAddress + 2 + i / 2);
        const int nibble = (i & 1u) == 0 ? (packed & 0x0F) : ((packed >> 4) & 0x0F);

        const int16_t decoded =
            decodeAdpcmNibble(nibble, shift, filter, voice.prevSample1, voice.prevSample2);
        voice.prevSample2 = voice.prevSample1;
        voice.prevSample1 = decoded;
        voice.decodedBlock[i] = decoded;
    }

    voice.decodedSampleIndex = 0;
    voice.blockLoaded = true;

    const bool loopEnd = (flags & 0x01) != 0;
    const bool loopRepeat = (flags & 0x02) != 0;
    const bool loopStart = (flags & 0x04) != 0;
    if (loopStart)
    {
        voice.repeatAddress = voice.currentAddress;
        voice.repeatAddressValid = true;
        const size_t voiceIndex = static_cast<size_t>(&voice - m_voices.data());
        m_registers[registerIndex(static_cast<u32>(voiceIndex) * kVoiceRegisterStride + 0x0Eu)] =
            voice.repeatAddress;
    }
    voice.blockLoopEnd = loopEnd;
    voice.blockLoopRepeat = loopRepeat;
}

int16_t Spu::decodeAdpcmNibble(int nibble, int shift, int filter, int prev1, int prev2) const
{
    int sample = nibble;
    if ((sample & 0x8) != 0)
    {
        sample -= 16;
    }

    const int shifted = (sample << 12) >> std::min(12, shift);
    const int prediction = ((prev1 * kFilterK0[filter]) + (prev2 * kFilterK1[filter]) + 32) / 64;
    return clampI16(shifted + prediction);
}

} // namespace runtime
} // namespace psxrecomp
