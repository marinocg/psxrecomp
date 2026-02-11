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
    std::fill(m_ram.begin(), m_ram.end(), 0);
    m_ramTransferCursor = 0;

    m_mixedAudioBuffer.clear();
    std::fill(m_reverbRing.begin(), m_reverbRing.end(), 0);
    m_reverbIndex = 0;
}

u16 Spu::readRegister(u32 offset) const
{
    return m_registers[registerIndex(offset)];
}

void Spu::writeRegister(u32 offset, u16 value)
{
    m_registers[registerIndex(offset)] = value;

    if (offset < VoiceCount * kVoiceRegisterStride)
    {
        const size_t voiceIndex = static_cast<size_t>(offset / kVoiceRegisterStride);
        const u32 voiceOffset = offset % kVoiceRegisterStride;
        onVoiceRegisterWrite(voiceIndex, voiceOffset, value);
        return;
    }

    onGlobalRegisterWrite(offset, value);
}

void Spu::tick(u32 cycles)
{
    m_cycles += cycles;
    m_pendingCycles += cycles;

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

void Spu::writeDma(u32 value)
{
    m_lastDmaWord = value;
    m_ram[m_ramTransferCursor] = value;
    m_ramTransferCursor = (m_ramTransferCursor + 1) % m_ram.size();
}

u32 Spu::readDma()
{
    const u32 value = m_ram[m_ramTransferCursor];
    m_ramTransferCursor = (m_ramTransferCursor + 1) % m_ram.size();
    return value;
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

void Spu::setAudioBackend(std::shared_ptr<SpuAudioBackend> backend)
{
    if (backend)
    {
        m_audioBackend = std::move(backend);
    }
}

size_t Spu::registerIndex(u32 offset)
{
    return (offset / 2) % 0x100;
}

Spu::Voice& Spu::voiceAt(size_t voiceIndex)
{
    return m_voices[voiceIndex % VoiceCount];
}

void Spu::onGlobalRegisterWrite(u32 offset, u16 value)
{
    switch (offset)
    {
    case RegisterMap::MainVolumeLeft:
        m_mixSettings.masterVolumeLeft = std::abs(normalizedSignedVolume(value));
        break;
    case RegisterMap::MainVolumeRight:
        m_mixSettings.masterVolumeRight = std::abs(normalizedSignedVolume(value));
        break;
    case RegisterMap::ReverbDepthLeft:
    case RegisterMap::ReverbDepthRight:
        m_mixSettings.reverbSend = std::abs(normalizedSignedVolume(value));
        break;
    case RegisterMap::KeyOnLow:
        applyVoiceMask(value, 0, true);
        break;
    case RegisterMap::KeyOnHigh:
        applyVoiceMask(0, value, true);
        break;
    case RegisterMap::KeyOffLow:
        applyVoiceMask(value, 0, false);
        break;
    case RegisterMap::KeyOffHigh:
        applyVoiceMask(0, value, false);
        break;
    case RegisterMap::ReverbOnLow:
        applyReverbMask(value, 0);
        break;
    case RegisterMap::ReverbOnHigh:
        applyReverbMask(0, value);
        break;
    case RegisterMap::RamTransferAddress:
        m_ramTransferCursor = (static_cast<size_t>(value) / 2) % m_ram.size();
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
    case 7:
        voice.repeatAddress = value;
        voice.repeatAddressValid = true;
        break;
    default:
        break;
    }
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
            voice.envelopePhase = Voice::EnvelopePhase::Attack;
            voice.envelopeLevel = 0.0f;
            voice.samplePosition = 0.0f;
            voice.currentAddress = voice.startAddress;
            voice.prevSample1 = 0;
            voice.prevSample2 = 0;
            voice.repeatAddressValid = false;
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

void Spu::applyReverbMask(u16 lowMask, u16 highMask)
{
    const u32 mask = static_cast<u32>(lowMask) | (static_cast<u32>(highMask) << 16);
    for (size_t voiceIndex = 0; voiceIndex < VoiceCount; ++voiceIndex)
    {
        voiceAt(voiceIndex).reverbEnabled = (mask & (1u << voiceIndex)) != 0;
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
    const float pitchStep = std::max(0.25f, static_cast<float>(voice.pitch) / 0x1000f);
    voice.samplePosition += pitchStep;

    while (voice.samplePosition >= 1.0f)
    {
        voice.samplePosition -= 1.0f;
        ++voice.decodedSampleIndex;
        if (voice.decodedSampleIndex >= voice.decodedBlock.size())
        {
            loadAdpcmBlock(voice);
            break;
        }
    }

    return sample;
}

void Spu::loadAdpcmBlock(Voice& voice)
{
    const size_t byteAddress =
        (static_cast<size_t>(voice.currentAddress) * 8) % (m_ram.size() * sizeof(u32));

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

    const bool blockEnd = (flags & 0x01) != 0;
    const bool loopBlock = (flags & 0x02) != 0;
    if (loopBlock)
    {
        voice.repeatAddress = voice.currentAddress;
        voice.repeatAddressValid = true;
    }

    if (blockEnd)
    {
        if (voice.repeatAddressValid)
        {
            voice.currentAddress = voice.repeatAddress;
        }
        else
        {
            voice.isActive = false;
            voice.envelopePhase = Voice::EnvelopePhase::Off;
        }
    }
    else
    {
        voice.currentAddress = static_cast<u16>(voice.currentAddress + 1);
    }
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

void Spu::mixQueuedSamples()
{
    if (!m_audioBackend || m_mixedAudioBuffer.empty())
    {
        return;
    }

    m_audioBackend->submitSamples(m_mixedAudioBuffer);
}

float Spu::normalizedSignedVolume(u16 value)
{
    const int16_t signedValue = static_cast<int16_t>(value);
    return static_cast<float>(signedValue) / 32767.0f;
}

} // namespace runtime
} // namespace psxrecomp
