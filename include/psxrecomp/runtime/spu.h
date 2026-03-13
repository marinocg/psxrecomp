#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class SpuAudioBackend
{
  public:
    virtual ~SpuAudioBackend() = default;
    virtual void submitSamples(const std::vector<int16_t>& interleavedStereoPcm) = 0;
};

class SpuNullAudioBackend final : public SpuAudioBackend
{
  public:
    void submitSamples(const std::vector<int16_t>& interleavedStereoPcm) override;

    const std::vector<int16_t>& lastSubmittedBuffer() const;

  private:
    std::vector<int16_t> m_lastSubmittedBuffer;
};

class Spu
{
  public:
    static constexpr size_t VoiceCount = 24;
    static constexpr size_t RamWordCount = 128u * 1024u / sizeof(u32);
    static constexpr size_t SamplesPerTick = 512;

    struct Voice
    {
        enum class EnvelopePhase
        {
            Attack,
            Decay,
            Sustain,
            Release,
            Off,
        };

        u16 leftVolume = 0;
        u16 rightVolume = 0;
        u16 pitch = 0;
        u16 startAddress = 0;
        u16 adsrLo = 0;
        u16 adsrHi = 0;
        u16 repeatAddress = 0;

        bool keyOn = false;
        bool keyOff = true;
        bool isActive = false;
        bool repeatAddressValid = false;
        bool pitchModulationEnabled = false;
        bool noiseEnabled = false;
        bool reverbEnabled = false;
        bool endx = false;
        bool blockLoopEnd = false;
        bool blockLoopRepeat = false;

        EnvelopePhase envelopePhase = EnvelopePhase::Off;
        float envelopeLevel = 0.0f;
        float samplePosition = 0.0f;
        u16 currentAddress = 0;

        int prevSample1 = 0;
        int prevSample2 = 0;

        std::array<int16_t, 28> decodedBlock{};
        size_t decodedSampleIndex = 28;
        bool blockLoaded = false;
    };

    struct MixSettings
    {
        float masterVolumeLeft = 1.0f;
        float masterVolumeRight = 1.0f;
        float reverbSend = 0.0f;
        float reverbFeedback = 0.25f;
    };

    struct RegisterMap
    {
        static constexpr u32 MainVolumeLeft = 0x760;
        static constexpr u32 MainVolumeRight = 0x762;
        static constexpr u32 ReverbDepthLeft = 0x764;
        static constexpr u32 ReverbDepthRight = 0x766;
        static constexpr u32 KeyOnLow = 0x788;
        static constexpr u32 KeyOnHigh = 0x78A;
        static constexpr u32 KeyOffLow = 0x78C;
        static constexpr u32 KeyOffHigh = 0x78E;
        static constexpr u32 PitchModulationLow = 0x790;
        static constexpr u32 PitchModulationHigh = 0x792;
        static constexpr u32 NoiseOnLow = 0x794;
        static constexpr u32 NoiseOnHigh = 0x796;
        static constexpr u32 ReverbOnLow = 0x798;
        static constexpr u32 ReverbOnHigh = 0x79A;
        static constexpr u32 EndxLow = 0x79C;
        static constexpr u32 EndxHigh = 0x79E;
        static constexpr u32 ReverbWorkAreaStart = 0x7A2;
        static constexpr u32 IrqAddress = 0x7A4;
        static constexpr u32 RamTransferAddress = 0x7A6;
        static constexpr u32 RamTransferData = 0x7A8;
        static constexpr u32 Control = 0x7AA;
        static constexpr u32 TransferControl = 0x7AC;
        static constexpr u32 Status = 0x7AE;
        static constexpr u32 CdVolumeLeft = 0x7B0;
        static constexpr u32 CdVolumeRight = 0x7B2;
        static constexpr u32 ExternalVolumeLeft = 0x7B4;
        static constexpr u32 ExternalVolumeRight = 0x7B6;
        static constexpr u32 CurrentMainVolumeLeft = 0x7B8;
        static constexpr u32 CurrentMainVolumeRight = 0x7BA;
    };

    Spu();

    void reset();

    u16 readRegister(u32 offset) const;
    void writeRegister(u32 offset, u16 value);

    void tick(u32 cycles);
    void writeDma(u32 value);
    u32 readDma();
    bool canTransferDma(bool fromRam) const;
    bool hasIrqRequest() const;
    void noteDmaTransfer(bool fromRam, u32 wordCount);

    u32 cyclesElapsed() const;
    u32 lastDmaWord() const;

    const std::vector<u32>& ramWords() const;
    const std::array<Voice, VoiceCount>& voices() const;
    const std::vector<int16_t>& mixedAudioBuffer() const;
    void pushCdAudioSamples(const std::vector<int16_t>& interleavedStereoPcm);
    size_t queuedCdAudioSamples() const;

    void setAudioBackend(std::shared_ptr<SpuAudioBackend> backend);

  private:
    static constexpr u32 CyclesPerSample = 768;

    static size_t registerIndex(u32 offset);
    static constexpr u32 canonicalRegisterOffset(u32 offset)
    {
        return offset & 0x1FFu;
    }

    Voice& voiceAt(size_t voiceIndex);

    u16 buildEndxRegister(bool high) const;
    u16 buildStatusRegister() const;
    bool transferControlNormal() const;
    bool irqControlEnabled() const;
    static u32 normalizeRamByteAddress(u32 byteAddress);
    void queueControlModeApply(u16 value);
    void tickControlState(u32 cycles);
    void runManualWriteTransfer();
    void maybeTriggerIrqRange(u32 startByteAddress, u32 byteCount);
    void onGlobalRegisterWrite(u32 offset, u16 value);
    void onVoiceRegisterWrite(size_t voiceIndex, u32 voiceOffset, u16 value);

    void applyVoiceMask(u16 lowMask, u16 highMask, bool keyOn);
    void applyPitchModulationMask(u32 voiceMask);
    void applyNoiseMask(u32 voiceMask);
    void applyReverbMask(u32 voiceMask);
    void updateEnvelope(Voice& voice);
    void finishVoiceBlock(Voice& voice);
    int16_t nextVoiceSample(Voice& voice);
    void loadAdpcmBlock(Voice& voice);
    int16_t decodeAdpcmNibble(int nibble, int shift, int filter, int prev1, int prev2) const;
    void mixQueuedSamples();
    static float normalizedSignedVolume(u16 value);

    std::array<u16, 0x100> m_registers{};
    std::array<Voice, VoiceCount> m_voices{};
    MixSettings m_mixSettings{};

    u32 m_cycles = 0;
    u32 m_pendingCycles = 0;
    u32 m_lastDmaWord = 0;
    u16 m_appliedControlBits = 0;
    u16 m_pendingControlBits = 0;
    u32 m_controlApplyCyclesRemaining = 0;
    u32 m_dmaReadRequestDelayCyclesRemaining = 0;
    u32 m_busyCyclesRemaining = 0;
    bool m_irqFlag = false;
    std::vector<u32> m_ram;
    u32 m_currentTransferAddress = 0;
    std::deque<u16> m_transferFifo;

    std::vector<int16_t> m_mixedAudioBuffer;
    std::vector<int16_t> m_reverbRing;
    size_t m_reverbIndex = 0;
    std::deque<int16_t> m_cdAudioRing;

    std::shared_ptr<SpuAudioBackend> m_audioBackend;
};

} // namespace runtime
} // namespace psxrecomp
