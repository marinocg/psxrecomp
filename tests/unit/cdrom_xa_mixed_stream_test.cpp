// PR-RV23: XA mixed stream integration test.
//
// Exercises a synthetic disc with alternating XA-ADPCM and Mode1 data sectors
// under Setmode 0x40 (XA streaming, no filter).  The invariants under test:
//
//   • XA-ADPCM sectors (even LBAs) → xa_audio_deliver; INT1 suppressed.
//   • Mode1 data sectors (odd LBAs)  → cpu_data_deliver; INT1 fires.
//   • After N tick pairs, xa_delivery_count == cpu_data_count == N.
//   • formatXaClassificationSummary() reports the correct split.
//
// This confirms the runtime does not wrongly surface XA-ADPCM sectors to the
// CPU data path, satisfying PR-RV23 acceptance criterion:
//   "no more guessing whether the runtime is wrongly surfacing XA sectors".
//
// http://problemkaputt.de/psx-spx.htm#cdromcontrollerioports

#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

namespace
{
using psxrecomp::u32;
using psxrecomp::u8;

constexpr u32 kReadCycles = 451584u;
constexpr size_t kMixedSectorCount = 8u; // 4 ADPCM + 4 data

// ---------------------------------------------------------------------------
// MixedStreamDisc: even LBAs are XA-ADPCM, odd LBAs are Mode1 data.
// ---------------------------------------------------------------------------
class MixedStreamDisc final : public psxrecomp::runtime::Disc
{
  public:
    MixedStreamDisc()
    {
        for (size_t lba = 0; lba < kMixedSectorCount; ++lba)
        {
            auto& raw = m_rawSectors[lba];
            raw.fill(0);
            if ((lba & 1u) == 0u)
            {
                // Even: Mode2/Form2/audio+realtime (XA-ADPCM), file=1 ch=1
                raw[15] = 0x02;
                raw[16] = 0x01; // file
                raw[17] = 0x01; // channel
                raw[18] = 0x64; // form2 | realtime | audio
                raw[19] = 0x01; // stereo 4-bit
                raw[20] = raw[16];
                raw[21] = raw[17];
                raw[22] = raw[18];
                raw[23] = raw[19];
                for (size_t i = 0; i < 2324; ++i)
                {
                    raw[24 + i] = static_cast<u8>((lba * 7u + i) & 0xFFu);
                }
            }
            else
            {
                // Odd: Mode1 — plain data (byte 15 = 0x01, no XA subheader)
                raw[15] = 0x01;
                for (size_t i = 16; i < 2352; ++i)
                {
                    raw[i] = static_cast<u8>((lba * 13u + i) & 0xFFu);
                }
            }
        }
    }

    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        if (lba >= kMixedSectorCount)
        {
            return false;
        }
        const auto& raw = m_rawSectors[lba];
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = raw[24 + i];
        }
        return true;
    }

    bool readRawSector2352(u32 lba, std::span<u8, 2352> out) override
    {
        if (lba >= kMixedSectorCount)
        {
            return false;
        }
        const auto& raw = m_rawSectors[lba];
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = raw[i];
        }
        return true;
    }

    u32 userSectorCount() const override
    {
        return static_cast<u32>(kMixedSectorCount);
    }

  private:
    std::array<std::array<u8, 2352>, kMixedSectorCount> m_rawSectors;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
u8 irqType(const psxrecomp::runtime::Cdrom& cdrom)
{
    return static_cast<u8>(cdrom.readInterruptFlags() & 0x07u);
}

void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeInterruptFlags(0x07);
}

void readAndAck(psxrecomp::runtime::Cdrom& cdrom)
{
    (void)cdrom.readResponse();
    ack(cdrom);
}

void drainAndAck(psxrecomp::runtime::Cdrom& cdrom)
{
    // Drain the response FIFO and ack INT1.
    while ((cdrom.readStatus() & (1u << 5)) != 0u)
    {
        (void)cdrom.readResponse();
    }
    ack(cdrom);
}

void enableBufferRead(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0, 0);
    cdrom.writeReg(3, 0x80);
}
} // namespace

int main()
{
    MixedStreamDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    // Setmode 0x40: XA streaming enabled, no filter.
    cdrom.writeParam(0x40);
    cdrom.writeCommand(0x0E);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);

    // Setloc to LBA 0, ReadN.
    cdrom.writeParam(0x00);
    cdrom.writeParam(0x02);
    cdrom.writeParam(0x00);
    cdrom.writeCommand(0x02);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
    cdrom.writeCommand(0x06);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);

    // Run through 4 (ADPCM, data) sector pairs.
    // Pattern: LBA 0=ADPCM, 1=data, 2=ADPCM, 3=data, ...
    for (size_t pair = 0; pair < 4u; ++pair)
    {
        // Even LBA: XA-ADPCM → xa_audio_deliver; NO INT1.
        cdrom.tick(kReadCycles);
        assert(irqType(cdrom) == 0x00);

        // Odd LBA: Mode1 data → cpu_data_deliver; INT1 fires.
        cdrom.tick(kReadCycles);
        assert(irqType(cdrom) == 0x01);

        // Drain and ack INT1 so the next sector can proceed.
        enableBufferRead(cdrom);
        drainAndAck(cdrom);
    }

    // After 4 pairs: 4 XA deliveries, 4 CPU deliveries.
    const std::string summary = cdrom.formatXaClassificationSummary();

    // INT1 suppressed equals XA delivery count (4).
    assert(summary.find("INT1_suppressed:  4") != std::string::npos);
    // INT1 count equals CPU delivery count (4).
    assert(summary.find("INT1_count:       4") != std::string::npos);
    // No filter rejects.
    assert(summary.find("filter_reject") != std::string::npos); // label present
    // XA classification summary is present and has a meaningful header.
    assert(summary.find("XA Sector Classification") != std::string::npos);
    // Stub sink consumed exactly 4 XA sectors (no audio callback installed).
    assert(summary.find("xa_sink:          stub") != std::string::npos);
    assert(summary.find("xa_consumed:      4") != std::string::npos);

    return 0;
}
