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
    spu.writeDma(packBytes(0x0C, 0x00, 0x11, 0x11));
    spu.writeDma(packBytes(0x11, 0x11, 0x11, 0x11));
    spu.writeDma(packBytes(0x11, 0x11, 0x11, 0x11));
    spu.writeDma(packBytes(0x11, 0x11, 0x11, 0x11));

    // Enable voice 0 with key-on and mixer settings.
    spu.writeRegister(Spu::RegisterMap::MainVolumeLeft, 0x3FFF);
    spu.writeRegister(Spu::RegisterMap::MainVolumeRight, 0x3FFF);
    spu.writeRegister(Spu::RegisterMap::ReverbDepthLeft, 0x2000);
    spu.writeRegister(Spu::RegisterMap::ReverbDepthRight, 0x2000);
    spu.writeRegister(Spu::RegisterMap::ReverbOnLow, 0x0001);

    auto backend = std::make_shared<CaptureBackend>();
    spu.setAudioBackend(backend);

    spu.writeRegister(Spu::RegisterMap::KeyOnLow, 0x0001);
    assert(spu.voices()[0].isActive);
    assert(spu.voices()[0].envelopePhase == Spu::Voice::EnvelopePhase::Attack);

    spu.tick(Spu::SamplesPerTick * 768);

    const auto& mixed = spu.mixedAudioBuffer();
    assert(!mixed.empty());
    assert(mixed.size() >= Spu::SamplesPerTick * 2);

    bool sawNonZero = false;
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

    // Key-off should transition to release and eventually disable the voice.
    spu.writeRegister(Spu::RegisterMap::KeyOffLow, 0x0001);
    assert(spu.voices()[0].envelopePhase == Spu::Voice::EnvelopePhase::Release);

    spu.tick(Spu::SamplesPerTick * 768);
    assert(!spu.voices()[0].isActive ||
           spu.voices()[0].envelopePhase == Spu::Voice::EnvelopePhase::Off);

    return 0;
}
