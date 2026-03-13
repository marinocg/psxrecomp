#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace
{
std::vector<psxrecomp::u8> makeXaAdpcmSector(psxrecomp::u8 fileNumber, psxrecomp::u8 channelNumber,
                                             psxrecomp::u8 packedNibbles)
{
    constexpr psxrecomp::u8 kMode2 = 0x02;
    constexpr psxrecomp::u8 kSubmodeAudioRealtimeForm2 = 0x64;
    constexpr psxrecomp::u8 kCodingStereo37800_4bit = 0x01;
    constexpr size_t kRawSectorBytes = 2352;
    constexpr size_t kSoundGroupCount = 18;
    constexpr size_t kSoundGroupBytes = 128;

    std::vector<psxrecomp::u8> raw(kRawSectorBytes, 0);
    raw[15] = kMode2;
    raw[16] = fileNumber;
    raw[17] = channelNumber;
    raw[18] = kSubmodeAudioRealtimeForm2;
    raw[19] = kCodingStereo37800_4bit;
    raw[20] = raw[16];
    raw[21] = raw[17];
    raw[22] = raw[18];
    raw[23] = raw[19];

    for (size_t group = 0; group < kSoundGroupCount; ++group)
    {
        const size_t base = 24 + group * kSoundGroupBytes;
        for (size_t headerByte = 0; headerByte < 8; ++headerByte)
        {
            // shift=12, filter=0 for deterministic decode (sample=sign-extended nibble).
            raw[base + 4 + headerByte] = 0x0C;
        }
        for (size_t i = 16; i < kSoundGroupBytes; ++i)
        {
            raw[base + i] = packedNibbles;
        }
    }

    return raw;
}

void writeCdromCommand(psxrecomp::runtime::PsxSystem& system, psxrecomp::u8 cmd)
{
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0u);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1, cmd);
}

void writeCdromParam(psxrecomp::runtime::PsxSystem& system, psxrecomp::u8 value)
{
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0u);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 2, value);
}

void issueSetmode(psxrecomp::runtime::PsxSystem& system, psxrecomp::u8 mode)
{
    writeCdromParam(system, mode);
    writeCdromCommand(system, 0x0E);
}

void issueSetfilter(psxrecomp::runtime::PsxSystem& system, psxrecomp::u8 fileNumber,
                    psxrecomp::u8 channelNumber)
{
    writeCdromParam(system, fileNumber);
    writeCdromParam(system, channelNumber);
    writeCdromCommand(system, 0x0D);
}

void ackCdromIrq(psxrecomp::runtime::PsxSystem& system)
{
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 1u);
    (void)system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3, 0x07u);
    system.tickCpuCycles(1);
}
} // namespace

int main()
{
    using psxrecomp::runtime::PsxSystem;

    // XA stereo decode should produce stable PCM and reach SPU mix output.
    {
        PsxSystem system;
        assert(system.initialize());

        system.cdrom().enqueueDataSector(makeXaAdpcmSector(0x01, 0x02, 0x21));
        issueSetmode(system, 0x40); // XA streaming enable
        ackCdromIrq(system);
        writeCdromCommand(system, 0x06); // ReadN
        ackCdromIrq(system);

        system.runFrame(); // CDROM decodes sector after SPU tick in this frame.
        assert(system.spu().queuedCdAudioSamples() > 0);

        system.runFrame(); // SPU consumes queued XA samples.
        const auto& mixed = system.spu().mixedAudioBuffer();
        assert(mixed.size() >= 256);

        int64_t checksum = 0;
        int64_t absSum = 0;
        for (size_t i = 0; i < 256; ++i)
        {
            checksum += mixed[i];
            absSum += mixed[i] < 0 ? -mixed[i] : mixed[i];
        }

        // 0x21 => left nibble=1, right nibble=2 with filter0/shift12 => deterministic 1,2 pattern.
        for (size_t frame = 0; frame < 64; ++frame)
        {
            assert(mixed[frame * 2 + 0] == 1);
            assert(mixed[frame * 2 + 1] == 2);
        }
        assert(checksum == 384);
        assert(absSum == 384);
    }

    // Setfilter must gate XA decode output before SPU mixing.
    {
        PsxSystem system;
        assert(system.initialize());

        system.cdrom().enqueueDataSector(makeXaAdpcmSector(0x03, 0x04, 0x43)); // non-matching
        system.cdrom().enqueueDataSector(makeXaAdpcmSector(0x01, 0x02, 0x21)); // matching

        issueSetmode(system, 0x48); // XA streaming + XA filter enable
        ackCdromIrq(system);
        issueSetfilter(system, 0x01, 0x02);
        ackCdromIrq(system);
        writeCdromCommand(system, 0x06); // ReadN
        ackCdromIrq(system);

        system.runFrame();
        assert(system.spu().queuedCdAudioSamples() > 0);

        system.runFrame();
        [[maybe_unused]] const auto& mixed = system.spu().mixedAudioBuffer();
        assert(mixed.size() >= 64);
        assert(mixed[0] == 1);
        assert(mixed[1] == 2);
        assert(!(mixed[0] == 3 && mixed[1] == 4));
    }

    return 0;
}
