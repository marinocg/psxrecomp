#include "psxrecomp/runtime/psx_system.h"
#include "psxrecomp/runtime/spu.h"

#include <cassert>
#include <cstdint>
#include <memory>

namespace
{
using psxrecomp::runtime::Spu;
using psxrecomp::runtime::SpuAudioBackend;

class CaptureBackend final : public SpuAudioBackend
{
  public:
    void submitSamples(const std::vector<int16_t>& interleavedStereoPcm) override
    {
        buffers.push_back(interleavedStereoPcm);
    }

    std::vector<std::vector<int16_t>> buffers;
};

constexpr psxrecomp::u32 packBytes(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    return static_cast<psxrecomp::u32>(b0) | (static_cast<psxrecomp::u32>(b1) << 8) |
           (static_cast<psxrecomp::u32>(b2) << 16) | (static_cast<psxrecomp::u32>(b3) << 24);
}

} // namespace

int main()
{
    using psxrecomp::runtime::PsxSystem;
    Spu spu;
    spu.reset();

    // Register map + voice model writes.
    spu.writeRegister(0x000, 0x2000);
    spu.writeRegister(0x002, 0x2000);
    spu.writeRegister(0x004, 0x1000);
    spu.writeRegister(0x006, 0x0000);
    spu.writeRegister(0x00E, 0x0001);

    assert(spu.readRegister(0x000) == 0x2000);
    assert(spu.voices()[0].leftVolume == 0x2000);
    assert(spu.voices()[0].pitch == 0x1000);

    // SPU RAM DMA write/read interaction.
    spu.writeRegister(Spu::RegisterMap::RamTransferAddress, 0x0000);
    spu.writeDma(0xDEADBEEFu);
    spu.writeDma(0x01020304u);
    assert(spu.ramWords()[0] == 0xDEADBEEFu);
    assert(spu.ramWords()[1] == 0x01020304u);
    assert(spu.lastDmaWord() == 0x01020304u);

    spu.writeRegister(Spu::RegisterMap::RamTransferAddress, 0x0000);
    assert(spu.readDma() == 0xDEADBEEFu);
    assert(spu.readDma() == 0x01020304u);

    // Simple ADPCM block with non-zero payload.
    spu.writeRegister(Spu::RegisterMap::RamTransferAddress, 0x0000);
    spu.writeDma(packBytes(0x00, 0x02, 0x77, 0x77));
    spu.writeDma(packBytes(0x77, 0x77, 0x77, 0x77));
    spu.writeDma(packBytes(0x77, 0x77, 0x77, 0x77));
    spu.writeDma(packBytes(0x77, 0x77, 0x77, 0x77));

    // Enable voice 0 with key-on and mixer settings.
    spu.writeRegister(Spu::RegisterMap::MainVolumeLeft, 0x3FFF);
    spu.writeRegister(Spu::RegisterMap::MainVolumeRight, 0x3FFF);
    spu.writeRegister(Spu::RegisterMap::ReverbDepthLeft, 0x2000);
    spu.writeRegister(Spu::RegisterMap::ReverbDepthRight, 0x2000);
    spu.writeRegister(Spu::RegisterMap::ReverbOnLow, 0x0001);
    assert(spu.voices()[0].reverbEnabled);
    spu.writeRegister(Spu::RegisterMap::ReverbOnHigh, 0x0001);
    assert(spu.voices()[0].reverbEnabled);
    assert(spu.voices()[16].reverbEnabled);

    auto backend = std::make_shared<CaptureBackend>();
    spu.setAudioBackend(backend);

    spu.writeRegister(Spu::RegisterMap::KeyOnLow, 0x0001);
    assert(spu.voices()[0].isActive);
    assert(spu.voices()[0].envelopePhase == Spu::Voice::EnvelopePhase::Attack);

    spu.tick(Spu::SamplesPerTick * 768);

    const auto& mixed = spu.mixedAudioBuffer();
    assert(!mixed.empty());
    assert(mixed.size() == Spu::SamplesPerTick * 2);

    [[maybe_unused]] bool sawNonZero = false;
    for (int16_t sample : mixed)
    {
        if (sample != 0)
        {
            sawNonZero = true;
            break;
        }
    }
    assert(sawNonZero);

    assert(!backend->buffers.empty());
    assert(!backend->buffers.back().empty());
    assert(backend->buffers.back().size() == mixed.size());

    // A second tick should not accumulate samples unboundedly.
    spu.tick(Spu::SamplesPerTick * 768);
    assert(spu.mixedAudioBuffer().size() == Spu::SamplesPerTick * 2);

    // Key-off should transition to release and eventually disable the voice.
    spu.writeRegister(Spu::RegisterMap::KeyOffLow, 0x0001);
    assert(spu.voices()[0].envelopePhase == Spu::Voice::EnvelopePhase::Release);

    spu.tick(Spu::SamplesPerTick * 768);
    assert(!spu.voices()[0].isActive ||
           spu.voices()[0].envelopePhase == Spu::Voice::EnvelopePhase::Off);

    // PsxSystem MMIO must preserve SPU bus width semantics.
    {
        PsxSystem system;
        assert(system.initialize());
        constexpr psxrecomp::u32 spuWindowMask = 0x1FFu;

        const psxrecomp::Address keyOnBase =
            psxrecomp::runtime::Mmio::SPU_BASE + (Spu::RegisterMap::KeyOnLow & spuWindowMask);
        const psxrecomp::Address keyOffBase =
            psxrecomp::runtime::Mmio::SPU_BASE + (Spu::RegisterMap::KeyOffLow & spuWindowMask);
        const psxrecomp::Address reverbBase =
            psxrecomp::runtime::Mmio::SPU_BASE + (Spu::RegisterMap::ReverbOnLow & spuWindowMask);
        const psxrecomp::Address endxBase =
            psxrecomp::runtime::Mmio::SPU_BASE + (Spu::RegisterMap::EndxLow & spuWindowMask);

        system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::SPU_BASE + 0x000u,
                                                 0x2468u);
        assert(system.spu().voices()[0].leftVolume == 0x2468u);
        assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::SPU_BASE +
                                                       0x000u) == 0x2468u);

        system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::SPU_BASE + 0x001u, 0x7Fu);
        assert(system.spu().voices()[0].leftVolume == 0x2468u);

        system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::SPU_BASE + 0x000u, 0x55u);
        assert(system.spu().voices()[0].leftVolume == 0x0055u);

        system.writeMmioExplicit<psxrecomp::u32>(keyOnBase, 0x00010001u);
        assert(system.spu().voices()[0].isActive);
        assert(system.spu().voices()[16].isActive);
        assert(system.spu().readRegister(Spu::RegisterMap::KeyOnLow) == 0x0001u);
        assert(system.spu().readRegister(Spu::RegisterMap::KeyOnHigh) == 0x0001u);
        assert(system.readMmioExplicit<psxrecomp::u32>(keyOnBase) == 0x00010001u);

        system.writeMmioExplicit<psxrecomp::u32>(reverbBase, 0x00010001u);
        assert(system.spu().voices()[0].reverbEnabled);
        assert(system.spu().voices()[16].reverbEnabled);
        assert(system.readMmioExplicit<psxrecomp::u32>(reverbBase) == 0x00010001u);

        system.writeMmioExplicit<psxrecomp::u32>(keyOffBase, 0x00010001u);
        assert(system.spu().voices()[0].envelopePhase == Spu::Voice::EnvelopePhase::Release);
        assert(system.spu().voices()[16].envelopePhase == Spu::Voice::EnvelopePhase::Release);
        assert(system.readMmioExplicit<psxrecomp::u32>(keyOffBase) == 0x00010001u);

        system.writeMmioExplicit<psxrecomp::u32>(endxBase, 0xA55AF00Du);
        assert(system.spu().readRegister(Spu::RegisterMap::EndxLow) == 0x0000u);
        assert(system.spu().readRegister(Spu::RegisterMap::EndxHigh) == 0x0000u);
        assert(system.readMmioExplicit<psxrecomp::u32>(endxBase) == 0x00000000u);
    }

    return 0;
}
