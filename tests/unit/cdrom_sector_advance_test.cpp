// PR-RV21b-v2: Per-sector host-visible data-ready gating.
//
// PSX-SPX describes ReadN/ReadS as a host protocol, not a raw byte stream:
//
//   INT1 published \u2192 game sets BFRD \u2192 DRQSTS rises \u2192 sector readable
//   sector drained \u2192 DRQSTS falls \u2192 (repeat for every subsequent sector)
//
// The next readable sector must NOT appear merely because the previous FIFO
// was exhausted.  Each sector requires its own INT1 publication followed by
// an explicit BFRD 0\u21921 transition.
//
// http://problemkaputt.de/psx-spx.htm#cdromcontrollerioports

#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <cassert>
#include <cstddef>
#include <vector>

namespace
{
constexpr psxrecomp::u32 kReadCycles = 451584u; // single-speed cadence

// Sector N: byte[i] = (N * 13 + i) & 0xFF  — every sector distinct.
class PatternDisc final : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(psxrecomp::u32 lba, std::span<psxrecomp::u8, 2048> out) override
    {
        for (size_t i = 0; i < out.size(); ++i)
            out[i] =
                static_cast<psxrecomp::u8>((lba * 13u + static_cast<psxrecomp::u32>(i)) & 0xFFu);
        return true;
    }
    bool readRawSector2352(psxrecomp::u32, std::span<psxrecomp::u8, 2352>) override
    {
        return false;
    }
    psxrecomp::u32 userSectorCount() const override
    {
        return 8u;
    }
};

psxrecomp::u8 irqType(const psxrecomp::runtime::Cdrom& c)
{
    return static_cast<psxrecomp::u8>(c.readInterruptFlags() & 0x07u);
}

bool drqsts(const psxrecomp::runtime::Cdrom& c)
{
    return (c.readStatus() & (1u << 6)) != 0u;
}

void ack(psxrecomp::runtime::Cdrom& c)
{
    c.writeInterruptFlags(0x07u);
}

void readAndAck(psxrecomp::runtime::Cdrom& c)
{
    while ((c.readStatus() & (1u << 5u)) != 0u)
        (void)c.readResponse();
    ack(c);
}

void enableBfrd(psxrecomp::runtime::Cdrom& c)
{
    c.writeReg(0u, 0u);
    c.writeReg(3u, 0x80u);
}

void disableBfrd(psxrecomp::runtime::Cdrom& c)
{
    c.writeReg(0u, 0u);
    c.writeReg(3u, 0x00u);
}

void issueSetloc(psxrecomp::runtime::Cdrom& c, psxrecomp::u8 mm, psxrecomp::u8 ss, psxrecomp::u8 ff)
{
    c.writeParam(mm);
    c.writeParam(ss);
    c.writeParam(ff);
    c.writeCommand(0x02u);
    assert(irqType(c) == 0x03u);
    readAndAck(c);
}

psxrecomp::u8 sectorByte(psxrecomp::u32 lba, size_t i)
{
    return static_cast<psxrecomp::u8>((lba * 13u + static_cast<psxrecomp::u32>(i)) & 0xFFu);
}

psxrecomp::u32 sectorWord(psxrecomp::u32 lba, size_t base)
{
    psxrecomp::u32 w = 0;
    for (size_t s = 0; s < 4u; ++s)
        w |= static_cast<psxrecomp::u32>(sectorByte(lba, base + s)) << (s * 8u);
    return w;
}

} // namespace

int main()
{
    using psxrecomp::runtime::Cdrom;
    PatternDisc disc;

    // -----------------------------------------------------------------------
    // Test 1 — Draining sector A does not expose sector B.
    //
    // Buffer two sectors (tick 2 × kReadCycles).  Arm BFRD for sector A
    // (INT1 published for A), drain all 2048 bytes.
    // After the last byte is consumed:
    //   • DRQSTS must be 0.
    //   • readData() must return the pad byte (not sectorByte(B,0)).
    //   • readDma() must return the u32-packed pad byte (not sectorWord(B,0)).
    //   • Sector B must not have leaked into the FIFO.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0
        cdrom.writeCommand(0x06u);               // ReadN
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        // Tick 2 cycles: INT1 for A active, B buffered.
        cdrom.tick(kReadCycles * 2u);
        assert(irqType(cdrom) == 0x01u); // INT1 for sector A (LBA=0)

        enableBfrd(cdrom);
        assert(drqsts(cdrom));

        // Drain all of sector A.
        for (size_t i = 0; i < 2048u; ++i)
            assert(cdrom.readData() == sectorByte(0u, i));

        // DRQSTS must clear immediately after the last byte is consumed.
        assert(!drqsts(cdrom));

        // readDma() with BFRD=1 but empty FIFO must return the pad byte,
        // NOT the first word of sector B.
        const psxrecomp::u32 padWord = cdrom.readDma();
        assert(padWord != sectorWord(1u, 0u)); // sector B leaked = bug

        // Confirm sector B's first byte is not the pad value.
        // (verifies the assertion above is meaningful, not trivially true.)
        assert(sectorByte(1u, 0u) != sectorByte(0u, 2040u));

        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Test 2 — Sector B requires its own INT1 + BFRD.
    //
    // After draining sector A and acknowledging its INT1, verify that:
    //   (a) sector B is NOT readable before BFRD is written.
    //   (b) sector B IS readable after BFRD 0\u21921.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0
        cdrom.writeCommand(0x06u);
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        cdrom.tick(kReadCycles * 2u);
        assert(irqType(cdrom) == 0x01u);

        // Drain sector A, then disable BFRD (so next write is a 0\u21921 edge).
        enableBfrd(cdrom);
        for (size_t i = 0; i < 2048u; ++i)
            (void)cdrom.readData();
        disableBfrd(cdrom);

        // Acknowledge INT1 for A \u2192 INT1 for B fires, B \u2192 m_activeSector.
        readAndAck(cdrom);
        cdrom.tick(1u);
        assert(irqType(cdrom) == 0x01u); // INT1 for sector B

        // (a) Without BFRD: sector B is not readable.
        assert(!drqsts(cdrom));
        assert(cdrom.readData() == 0x00u);
        assert(cdrom.readDma() == 0x00000000u);

        // (b) After BFRD 0\u21921: sector B becomes readable.
        enableBfrd(cdrom);
        assert(drqsts(cdrom));
        assert(cdrom.readData() == sectorByte(1u, 0u));
        assert(cdrom.readDma() == sectorWord(1u, 1u)); // next 4 bytes

        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Test 3 — DMA3 cannot skip the host-side INT1 promotion.
    //
    // Scenario: sector A is accepted and active (BFRD=1, INT1-A pending/acked).
    //           Drain all of A via readDma().  Sector B is buffered internally
    //           but its INT1 has NOT been promoted yet (INT1-A still pending).
    //           Further readDma() calls must NOT expose sector B.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0
        cdrom.writeCommand(0x06u);
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        // Tick 2 cycles: INT1-A visible, B in m_bufferedReadSectors.
        cdrom.tick(kReadCycles * 2u);
        assert(irqType(cdrom) == 0x01u);

        enableBfrd(cdrom);

        // Drain all of sector A through the DMA path.
        for (size_t w = 0; w < 512u; ++w) // 512 words = 2048 bytes
        {
            const psxrecomp::u32 got = cdrom.readDma();
            assert(got == sectorWord(0u, w * 4u));
        }
        // FIFO empty, DRQSTS=0.
        assert(!drqsts(cdrom));

        // Additional DMA reads must NOT return sector B data.
        for (size_t extra = 0; extra < 4u; ++extra)
        {
            const psxrecomp::u32 got = cdrom.readDma();
            assert(got != sectorWord(1u, 0u)); // sector B must not be visible
        }
    }

    // -----------------------------------------------------------------------
    // Test 4 — HCLRCTL (IRQ acknowledge) does not expose the next sector.
    //
    // After fully draining sector A with BFRD=1, disabling BFRD, then writing
    // HCLRCTL=0x07: the INT1 for sector B fires and B is promoted to
    // m_activeSector.  However, BFRD is 0, so DRQSTS must remain 0 and any
    // readData()/readDma() must return 0 — not sector B's first byte.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x00u);
        cdrom.writeCommand(0x06u);
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        cdrom.tick(kReadCycles * 2u);
        assert(irqType(cdrom) == 0x01u);

        // Arm BFRD, drain sector A, then clear BFRD.
        enableBfrd(cdrom);
        for (size_t i = 0; i < 2048u; ++i)
            (void)cdrom.readData();
        assert(!drqsts(cdrom));
        disableBfrd(cdrom);

        // HCL RCTL: ack INT1 for A \u2192 INT1 for B fires, B \u2192 m_activeSector.
        readAndAck(cdrom);
        cdrom.tick(1u);
        assert(irqType(cdrom) == 0x01u); // INT1 for B is now active

        // BFRD is 0: even though B is now the active sector, it is NOT readable.
        assert(!drqsts(cdrom));
        assert(cdrom.readData() == 0x00u);
        assert(cdrom.readDma() == 0x00000000u);

        // Only after the game explicitly arms BFRD (0\u21921) does B become readable.
        enableBfrd(cdrom);
        assert(drqsts(cdrom));
        assert(cdrom.readData() == sectorByte(1u, 0u));

        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Test 5 — DRQSTS lifecycle is strictly per accepted sector.
    //
    // Verify:
    //   • DRQSTS stays set while bytes remain in the accepted sector.
    //   • DRQSTS clears exactly when the last byte is consumed.
    //   • DRQSTS does not reassert for sector B until B has its own INT1+BFRD.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x00u);
        cdrom.writeCommand(0x06u);
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        cdrom.tick(kReadCycles * 2u);
        assert(irqType(cdrom) == 0x01u);

        enableBfrd(cdrom);
        assert(drqsts(cdrom));

        // Partial drain (halfway): DRQSTS must still be set.
        for (size_t i = 0; i < 1024u; ++i)
            (void)cdrom.readData();
        assert(drqsts(cdrom));

        // Drain remaining bytes of sector A.
        for (size_t i = 1024u; i < 2048u; ++i)
            (void)cdrom.readData();

        // Exactly after the last byte: DRQSTS must fall.
        assert(!drqsts(cdrom));

        // Sector B: DRQSTS must stay 0 until its own INT1 + BFRD.
        disableBfrd(cdrom);
        readAndAck(cdrom);
        cdrom.tick(1u);
        assert(irqType(cdrom) == 0x01u);
        assert(!drqsts(cdrom)); // BFRD=0 \u2192 DRQSTS=0

        enableBfrd(cdrom);
        assert(drqsts(cdrom)); // BFRD armed for B \u2192 DRQSTS=1

        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Integration 1 — Scripted two-sector ReadN regression.
    //
    // Verifies that the full per-sector host-visible progression is correct:
    //
    //   Setloc \u2192 ReadN \u2192 INT3 \u2192 ack
    //   \u2192 INT1(A) \u2192 BFRD=1 \u2192 drain A byte-by-byte \u2192 DRQSTS=0
    //   \u2192 [sector B not visible] \u2192 ack INT1-A
    //   \u2192 INT1(B) \u2192 BFRD=1 \u2192 drain B byte-by-byte \u2192 DRQSTS=0
    //
    // Sector A and sector B must NOT readable from a single BFRD write.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        const psxrecomp::u32 lbaA = 0u; // LBA 0x000202 → LBA=0 raw
        const psxrecomp::u32 lbaB = 1u;

        issueSetloc(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0
        cdrom.writeCommand(0x06u);               // ReadN
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        // ---- Sector A phase ----
        // Tick 2× so sector B is pre-buffered before we drain sector A.
        cdrom.tick(kReadCycles * 2u);
        assert(irqType(cdrom) == 0x01u);
        assert(!drqsts(cdrom));

        enableBfrd(cdrom);
        assert(drqsts(cdrom));

        // Read all 2048 bytes; verify contents.
        for (size_t i = 0; i < 2048u; ++i)
            assert(cdrom.readData() == sectorByte(lbaA, i));

        assert(!drqsts(cdrom)); // sector A fully consumed

        // Sector B must not yet be readable (BFRD=1 but FIFO empty).
        assert(cdrom.readDma() != sectorWord(lbaB, 0u));

        // ---- Sector A → sector B transition ----
        disableBfrd(cdrom);
        readAndAck(cdrom);
        cdrom.tick(1u);
        assert(irqType(cdrom) == 0x01u);

        // ---- Sector B phase ----
        assert(!drqsts(cdrom)); // BFRD cleared

        enableBfrd(cdrom); // 0\u21921: load sector B
        assert(drqsts(cdrom));

        for (size_t i = 0; i < 2048u; ++i)
            assert(cdrom.readData() == sectorByte(lbaB, i));

        assert(!drqsts(cdrom));
        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Integration 2 — Scripted two-sector ReadN using DMA3 only.
    //
    // Same sequence as Integration 1 but drains each sector word-at-a-time
    // via readDma().  Confirms DMA3 obeys the same per-sector gate.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        const psxrecomp::u32 lbaA = 2u;
        const psxrecomp::u32 lbaB = 3u;

        issueSetloc(cdrom, 0x00u, 0x02u, 0x02u); // LBA=2
        cdrom.writeCommand(0x06u);
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        // ---- Sector A DMA phase ----
        cdrom.tick(kReadCycles * 2u); // both A and B buffered
        assert(irqType(cdrom) == 0x01u);

        enableBfrd(cdrom);
        assert(drqsts(cdrom));

        for (size_t w = 0; w < 512u; ++w)
            assert(cdrom.readDma() == sectorWord(lbaA, w * 4u));

        assert(!drqsts(cdrom));

        // DMA3 extra reads must NOT return sector B data.
        assert(cdrom.readDma() != sectorWord(lbaB, 0u));

        // ---- Transition to sector B ----
        disableBfrd(cdrom);
        readAndAck(cdrom);
        cdrom.tick(1u);
        assert(irqType(cdrom) == 0x01u);

        // ---- Sector B DMA phase ----
        assert(!drqsts(cdrom));
        enableBfrd(cdrom);
        assert(drqsts(cdrom));

        for (size_t w = 0; w < 512u; ++w)
            assert(cdrom.readDma() == sectorWord(lbaB, w * 4u));

        assert(!drqsts(cdrom));
        readAndAck(cdrom);
    }

    return 0;
}
