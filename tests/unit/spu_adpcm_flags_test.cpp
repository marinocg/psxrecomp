#include "psxrecomp/runtime/spu.h"

#include <sstream>
#include <stdexcept>

namespace
{
using psxrecomp::u16;
using psxrecomp::u32;
using psxrecomp::u8;
using psxrecomp::runtime::Spu;

constexpr u32 kCyclesPerSample = 768u;

constexpr u32 packBytes(u8 b0, u8 b1, u8 b2, u8 b3)
{
    return static_cast<u32>(b0) | (static_cast<u32>(b1) << 8) | (static_cast<u32>(b2) << 16) |
           (static_cast<u32>(b3) << 24);
}

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

[[noreturn]] void failValue(const char* message, u32 value)
{
    std::ostringstream stream;
    stream << message << " value=0x" << std::hex << value;
    throw std::runtime_error(stream.str());
}

void writeAdpcmBlock(Spu& spu, u16 addressUnits, u8 flags, u8 payloadByte)
{
    spu.writeRegister(Spu::RegisterMap::RamTransferAddress, addressUnits);
    spu.writeDma(packBytes(0x00u, flags, payloadByte, payloadByte));
    spu.writeDma(packBytes(payloadByte, payloadByte, payloadByte, payloadByte));
    spu.writeDma(packBytes(payloadByte, payloadByte, payloadByte, payloadByte));
    spu.writeDma(packBytes(payloadByte, payloadByte, payloadByte, payloadByte));
}
} // namespace

int main()
{
    Spu spu;
    spu.reset();

    // Voice 0: end+mute using a software-programmed repeat address.
    // Voice 1: loop-start followed by end+repeat.
    writeAdpcmBlock(spu, 0x0000u, 0x01u, 0x11u);
    writeAdpcmBlock(spu, 0x0002u, 0x04u, 0x22u);
    writeAdpcmBlock(spu, 0x0004u, 0x03u, 0x33u);

    spu.writeRegister(0x000u, 0x3FFFu);
    spu.writeRegister(0x002u, 0x3FFFu);
    spu.writeRegister(0x004u, 0x1000u);
    spu.writeRegister(0x006u, 0x0000u);
    spu.writeRegister(0x00Eu, 0x0006u);

    spu.writeRegister(0x010u, 0x3FFFu);
    spu.writeRegister(0x012u, 0x3FFFu);
    spu.writeRegister(0x014u, 0x1000u);
    spu.writeRegister(0x016u, 0x0002u);
    spu.writeRegister(0x01Eu, 0x0010u);

    spu.writeRegister(Spu::RegisterMap::KeyOnLow, 0x0003u);
    require((spu.readRegister(Spu::RegisterMap::EndxLow) & 0x0003u) == 0u,
            "KON should start with ENDX clear");

    spu.tick(28u * kCyclesPerSample);

    if ((spu.readRegister(Spu::RegisterMap::EndxLow) & 0x0001u) == 0u)
    {
        const u32 state = (static_cast<u32>(spu.voices()[0].currentAddress) << 16) |
                          static_cast<u32>(spu.voices()[0].decodedSampleIndex);
        failValue("loop-end should set ENDX for one-shot voice (currentAddress<<16|sampleIndex)",
                  state);
    }
    require((spu.readRegister(Spu::RegisterMap::EndxLow) & 0x0002u) == 0u,
            "voice 1 should not hit ENDX before its loop-end block");
    require(spu.voices()[0].envelopePhase == Spu::Voice::EnvelopePhase::Off,
            "end+mute should drive the one-shot voice to silence");
    require(!spu.voices()[0].isActive, "one-shot voice should stop after loop-end without repeat");
    if (spu.voices()[0].currentAddress != 0x0006u)
    {
        failValue("software-written repeat address was lost across KON",
                  spu.voices()[0].currentAddress);
    }
    require(spu.voices()[1].repeatAddress == 0x0002u, "loop-start did not capture repeat address");
    require(spu.readRegister(0x01Eu) == 0x0002u,
            "loop-start did not update repeat register readback");
    if (spu.voices()[1].currentAddress != 0x0004u)
    {
        failValue("voice 1 did not advance to the next ADPCM block",
                  spu.voices()[1].currentAddress);
    }

    spu.tick(28u * kCyclesPerSample);

    require((spu.readRegister(Spu::RegisterMap::EndxLow) & 0x0003u) == 0x0003u,
            "both voices should have ENDX set after loop-end");
    require(spu.voices()[1].isActive, "looping voice should remain active after end+repeat");
    if (spu.voices()[1].currentAddress != 0x0002u)
    {
        failValue("end+repeat did not jump back to the repeat address",
                  spu.voices()[1].currentAddress);
    }

    spu.writeRegister(Spu::RegisterMap::KeyOnLow, 0x0002u);
    require((spu.readRegister(Spu::RegisterMap::EndxLow) & 0x0002u) == 0u,
            "KON should clear ENDX for the keyed voice");
    require((spu.readRegister(Spu::RegisterMap::EndxLow) & 0x0001u) != 0u,
            "KON should not clear ENDX for unrelated voices");

    return 0;
}
