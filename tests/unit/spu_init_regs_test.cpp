#include "psxrecomp/runtime/spu.h"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace
{
using psxrecomp::u16;
using psxrecomp::u32;
using psxrecomp::runtime::Spu;

constexpr u32 kHandshakeDelayCycles = 0x300u;
constexpr u32 kCyclesPerSample = 768u;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
} // namespace

int main()
{
    Spu spu;
    spu.reset();

    spu.writeRegister(Spu::RegisterMap::PitchModulationLow, 0x0003u);
    spu.writeRegister(Spu::RegisterMap::PitchModulationHigh, 0x0001u);
    require(!spu.voices()[0].pitchModulationEnabled, "voice 0 pitch modulation should stay off");
    require(spu.voices()[1].pitchModulationEnabled, "voice 1 pitch modulation did not latch");
    require(spu.voices()[16].pitchModulationEnabled, "voice 16 pitch modulation did not latch");

    spu.writeRegister(Spu::RegisterMap::NoiseOnLow, 0x0005u);
    spu.writeRegister(Spu::RegisterMap::NoiseOnHigh, 0x0001u);
    require(spu.voices()[0].noiseEnabled, "voice 0 noise enable did not latch");
    require(spu.voices()[2].noiseEnabled, "voice 2 noise enable did not latch");
    require(spu.voices()[16].noiseEnabled, "voice 16 noise enable did not latch");

    spu.writeRegister(Spu::RegisterMap::ReverbOnLow, 0x0002u);
    spu.writeRegister(Spu::RegisterMap::ReverbOnHigh, 0x0001u);
    require(spu.voices()[1].reverbEnabled, "voice 1 reverb enable did not latch");
    require(spu.voices()[16].reverbEnabled, "voice 16 reverb enable did not latch");

    spu.writeRegister(Spu::RegisterMap::ReverbWorkAreaStart, 0x2345u);
    require(spu.readRegister(Spu::RegisterMap::ReverbWorkAreaStart) == 0x2345u,
            "reverb work-area start did not read back");

    spu.writeRegister(Spu::RegisterMap::MainVolumeLeft, 0x2000u);
    spu.writeRegister(Spu::RegisterMap::MainVolumeRight, 0x3000u);
    require(spu.readRegister(Spu::RegisterMap::CurrentMainVolumeLeft) == 0x2000u,
            "current main volume left did not reflect main volume");
    require(spu.readRegister(Spu::RegisterMap::CurrentMainVolumeRight) == 0x3000u,
            "current main volume right did not reflect main volume");

    spu.writeRegister(0x00Cu, 0x2000u);
    require(std::abs(spu.voices()[0].envelopeLevel - (8192.0f / 32767.0f)) < 0.01f,
            "current ADSR write did not update envelope state");
    require(std::abs(static_cast<int>(spu.readRegister(0x00Cu)) - 0x2000) <= 1,
            "current ADSR readback did not reflect envelope state");

    // CD audio routing bits are part of the delayed SPUCNT[5:0] apply path.
    spu.writeRegister(Spu::RegisterMap::MainVolumeLeft, 0x3FFFu);
    spu.writeRegister(Spu::RegisterMap::MainVolumeRight, 0x3FFFu);

    spu.writeRegister(Spu::RegisterMap::Control, 0x0000u);
    spu.tick(kHandshakeDelayCycles);
    spu.pushCdAudioSamples(std::vector<int16_t>{12000, 12000});
    spu.tick(kCyclesPerSample);
    require(spu.mixedAudioBuffer().size() == 2u, "unexpected mixed buffer size with CD disabled");
    require(spu.mixedAudioBuffer()[0] == 0 && spu.mixedAudioBuffer()[1] == 0,
            "CD audio leaked through with routing disabled");

    spu.writeRegister(Spu::RegisterMap::Control, 0x0001u);
    spu.tick(kHandshakeDelayCycles);
    require((spu.readRegister(Spu::RegisterMap::Status) & 0x000Fu) == 0x0001u,
            "CD audio routing bit did not apply through SPUSTAT");
    spu.pushCdAudioSamples(std::vector<int16_t>{12000, 12000});
    spu.tick(kCyclesPerSample);
    require(spu.mixedAudioBuffer().size() == 2u, "unexpected mixed buffer size with CD enabled");
    require(spu.mixedAudioBuffer()[0] != 0 || spu.mixedAudioBuffer()[1] != 0,
            "CD audio routing enable did not affect the mixer");

    return 0;
}
