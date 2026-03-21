// PR-RV55: Mixed-XA CPU delivery correctness tests.
//
// Verifies two behavioural contracts:
//
//   1. Unread-remainder replaced (PR-RV55): when BFRD=1 is held across a
//      sector boundary and the game partially reads the first sector (leaving
//      bytes in the FIFO), INT1 delivery for the next CPU sector discards the
//      stale remainder and immediately exposes the new sector's payload.
//
//   2. DMA3 == CPU RDDAT parity: both readDma() and readData() drain the same
//      data FIFO byte-for-byte, so a game switching between DMA3 and register
//      reads sees identical sector content.
//
// Disc layout used by test 1 (MixedXaDisc):
//   LBA 0 — Mode1 user-data   (2048 bytes; pattern base 0xA0)
//   LBA 1 — Mode2/Form2 XA-ADPCM (audio sink, no INT1)
//   LBA 2 — Mode2/Form2 non-audio (submode_reject → CPU, 2324 bytes; pattern base 0xB0)
//
// http://problemkaputt.de/psx-spx.htm#cdromcontrollerioports

#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <vector>

namespace
{
using psxrecomp::u32;
using psxrecomp::u8;

constexpr u32 kReadCycles = 451584u;

// ---------------------------------------------------------------------------
// MixedXaDisc: Mode1 + XA-ADPCM + Form2-non-audio.
// ---------------------------------------------------------------------------
class MixedXaDisc final : public psxrecomp::runtime::Disc
{
  public:
    MixedXaDisc()
    {
        // LBA 0: Mode1 plain user data, pattern base 0xA0.
        auto& s0 = m_raw[0];
        s0.fill(0);
        s0[15] = 0x01;
        for (size_t i = 0; i < 2048; ++i)
            s0[24 + i] = static_cast<u8>((i + 0xA0u) & 0xFFu);

        // LBA 1: Mode2/Form2/audio+realtime — XA-ADPCM (→ audio sink, no INT1).
        auto& s1 = m_raw[1];
        s1.fill(0);
        s1[15] = 0x02;
        s1[16] = 0x01;
        s1[17] = 0x02;
        s1[18] = 0x64; // form2(0x20)|realtime(0x40)|audio(0x04)
        s1[19] = 0x01; // stereo 4-bit
        s1[20] = s1[16];
        s1[21] = s1[17];
        s1[22] = s1[18];
        s1[23] = s1[19];
        for (size_t i = 0; i < 2324; ++i)
            s1[24 + i] = static_cast<u8>((i + 0xC0u) & 0xFFu);

        // LBA 2: Mode2/Form2 non-audio (MDEC-style) — submode_reject → CPU,
        //        2324 bytes, pattern base 0xB0.
        auto& s2 = m_raw[2];
        s2.fill(0);
        s2[15] = 0x02;
        s2[16] = 0x01;
        s2[17] = 0x03;
        s2[18] = 0x20; // form2 only; no audio, no realtime
        s2[19] = 0x00;
        s2[20] = s2[16];
        s2[21] = s2[17];
        s2[22] = s2[18];
        s2[23] = s2[19];
        for (size_t i = 0; i < 2324; ++i)
            s2[24 + i] = static_cast<u8>((i + 0xB0u) & 0xFFu);
    }

    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        if (lba >= 3u)
            return false;
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = m_raw[lba][24 + i];
        return true;
    }

    bool readRawSector2352(u32 lba, std::span<u8, 2352> out) override
    {
        if (lba >= 3u)
            return false;
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = m_raw[lba][i];
        return true;
    }

    u32 userSectorCount() const override
    {
        return 3u;
    }
    u8 rawByte(u32 lba, size_t offset) const
    {
        return m_raw[lba][offset];
    }

  private:
    std::array<std::array<u8, 2352>, 3> m_raw;
};

// ---------------------------------------------------------------------------
// Helpers (shared with cdrom_cpu_payload_test pattern)
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

void enableBfrd(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0, 0);
    cdrom.writeReg(3, 0x80);
}

void issueSetmode(psxrecomp::runtime::Cdrom& cdrom, u8 mode)
{
    cdrom.writeParam(mode);
    cdrom.writeCommand(0x0E);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
}

void issueSetloc(psxrecomp::runtime::Cdrom& cdrom, u8 mm, u8 ss, u8 ff)
{
    cdrom.writeParam(mm);
    cdrom.writeParam(ss);
    cdrom.writeParam(ff);
    cdrom.writeCommand(0x02);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
}

void issueReadN(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeCommand(0x06);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
}
} // namespace

// ---------------------------------------------------------------------------
// Test 1: Unread-remainder is replaced when BFRD is held across a sector
//         boundary — PR-RV55 semantics.
//
// Game reads only 511 of 512 words from a 2048-byte Mode1 sector (leaving
// 4 bytes in the FIFO), ACKs INT1, and keeps BFRD=1.  An XA-ADPCM sector
// arrives (no INT1), then a Form2 non-audio sector fires INT1.
//
// PR-RV55: INT1 delivery discards the 4 stale LBA 0 bytes and immediately
// exposes LBA 2 raw[24..27] = { 0xB0, 0xB1, 0xB2, 0xB3 }.
// ---------------------------------------------------------------------------
void testUnreadRemainderReplacedAtInt1()
{
    MixedXaDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);            // XA streaming on
    issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA 0
    issueReadN(cdrom);

    // --- LBA 0 (Mode1, 2048 bytes) ---
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);
    // Read only 511 of 512 words — 4 bytes intentionally left in FIFO.
    for (int i = 0; i < 511; ++i)
        (void)cdrom.readDma();
    while ((cdrom.readStatus() & (1u << 5)) != 0u)
        (void)cdrom.readResponse();
    ack(cdrom); // ACK INT1; BFRD stays 1

    // --- LBA 1 (XA-ADPCM) — no INT1 ---
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x00);

    // --- LBA 2 (Form2 non-audio) — INT1; PR-RV55 must replace FIFO remainder ---
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    // DRQSTS (bit 6) remains set because LBA 2 payload is immediately readable.
    assert((cdrom.readStatus() & 0x40u) != 0u);

    // First DMA word must be LBA 2 raw[24..27] = { 0xB0, 0xB1, 0xB2, 0xB3 } (LE).
    const u32 expectedLba2Word = static_cast<u32>(0xB0u) | (static_cast<u32>(0xB1u) << 8) |
                                 (static_cast<u32>(0xB2u) << 16) | (static_cast<u32>(0xB3u) << 24);
    assert(cdrom.readDma() == expectedLba2Word);
}

// ---------------------------------------------------------------------------
// Test 2: DMA3 readDma() == CPU readData() byte parity for the same sector.
//
// Two fresh cdrom instances share the same disc.  Instance A drains LBA 0
// via readDma(); instance B drains LBA 0 via readData().  Each 4-byte group
// read by readDma() must equal the same four bytes read sequentially by
// readData(), confirming that both paths present identical sector content.
// ---------------------------------------------------------------------------
void testDma3EqualsCpuRddat()
{
    MixedXaDisc disc;
    constexpr int kWords = 512; // 512 × 4 = 2048 bytes

    // --- Instance A: read via DMA3 ---
    std::vector<u32> dmaWords;
    dmaWords.reserve(kWords);
    {
        psxrecomp::runtime::Cdrom cdromA;
        cdromA.reset();
        cdromA.setDiscBackend(&disc);
        cdromA.writeInterruptEnable(0x1F);
        issueSetmode(cdromA, 0x40);
        issueSetloc(cdromA, 0x00, 0x02, 0x00); // LBA 0
        issueReadN(cdromA);
        cdromA.tick(kReadCycles);
        assert(irqType(cdromA) == 0x01);
        enableBfrd(cdromA);
        for (int i = 0; i < kWords; ++i)
            dmaWords.push_back(cdromA.readDma());
    }

    // --- Instance B: read via CPU RDDAT ---
    std::vector<u8> cpuBytes;
    cpuBytes.reserve(kWords * 4);
    {
        psxrecomp::runtime::Cdrom cdromB;
        cdromB.reset();
        cdromB.setDiscBackend(&disc);
        cdromB.writeInterruptEnable(0x1F);
        issueSetmode(cdromB, 0x40);
        issueSetloc(cdromB, 0x00, 0x02, 0x00); // LBA 0
        issueReadN(cdromB);
        cdromB.tick(kReadCycles);
        assert(irqType(cdromB) == 0x01);
        enableBfrd(cdromB);
        for (int i = 0; i < kWords * 4; ++i)
            cpuBytes.push_back(cdromB.readData());
    }

    // Compare word-by-word (little-endian).
    for (int i = 0; i < kWords; ++i)
    {
        const u32 cpuWord = static_cast<u32>(cpuBytes[static_cast<size_t>(i * 4)]) |
                            (static_cast<u32>(cpuBytes[static_cast<size_t>(i * 4 + 1)]) << 8) |
                            (static_cast<u32>(cpuBytes[static_cast<size_t>(i * 4 + 2)]) << 16) |
                            (static_cast<u32>(cpuBytes[static_cast<size_t>(i * 4 + 3)]) << 24);
        assert(dmaWords[static_cast<size_t>(i)] == cpuWord);
    }
}

int main()
{
    testUnreadRemainderReplacedAtInt1();
    testDma3EqualsCpuRddat();
    return 0;
}
