// Tests for DRQSTS gating and the host-side data-ready handshake.
//
// PSX-SPX:  1F801800h bit 6 = DRQSTS (Data Request Status) — set only when
// both BFRD (bit 7 of the REQUEST register at bank 0, offset 3) is active and
// the data FIFO contains bytes ready for the host to read.
//
// The canonical host-side sequence after ReadN is:
//   INT1 observed  ->  BFRD=1  ->  DRQSTS=1  ->  RDDATA / DMA3 valid
//   drain sector   ->  DRQSTS=0
//
// None of the intermediate steps should be short-circuited: BFRD must be
// written explicitly, and DMA3 must obey the same gate as the CPU data port.
//
// http://problemkaputt.de/psx-spx.htm#cdromcontrollerioports

#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <cassert>
#include <cstddef>
#include <vector>

namespace
{
constexpr psxrecomp::u32 kReadCycles = 451584u; // 33.8688 MHz / 75 sectors/sec (1x)

// Sector N fills with (N * 11 + byte_index) & 0xFF so every sector is distinct.
class PatternDisc final : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(psxrecomp::u32 lba, std::span<psxrecomp::u8, 2048> out) override
    {
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] =
                static_cast<psxrecomp::u8>((lba * 11u + static_cast<psxrecomp::u32>(i)) & 0xFFu);
        }
        return true;
    }

    bool readRawSector2352(psxrecomp::u32 /*lba*/, std::span<psxrecomp::u8, 2352> /*out*/) override
    {
        return false;
    }

    psxrecomp::u32 userSectorCount() const override
    {
        return 8u;
    }
};

psxrecomp::u8 irqType(const psxrecomp::runtime::Cdrom& cdrom)
{
    return static_cast<psxrecomp::u8>(cdrom.readInterruptFlags() & 0x07u);
}

// DRQSTS = bit 6 of the status register.
bool drqsts(const psxrecomp::runtime::Cdrom& cdrom)
{
    return (cdrom.readStatus() & (1u << 6)) != 0u;
}

void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeInterruptFlags(0x07u);
}

void enableBufferRead(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0u, 0u); // bank 0
    cdrom.writeReg(3u, 0x80u);
}

void disableBufferRead(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0u, 0u); // bank 0
    cdrom.writeReg(3u, 0x00u);
}

void readAndAck(psxrecomp::runtime::Cdrom& cdrom)
{
    while ((cdrom.readStatus() & (1u << 5u)) != 0u)
    {
        (void)cdrom.readResponse();
    }
    ack(cdrom);
}

void issueSetloc(psxrecomp::runtime::Cdrom& cdrom, psxrecomp::u8 mm, psxrecomp::u8 ss,
                 psxrecomp::u8 ff)
{
    cdrom.writeParam(mm);
    cdrom.writeParam(ss);
    cdrom.writeParam(ff);
    cdrom.writeCommand(0x02u);
    assert(irqType(cdrom) == 0x03u);
    readAndAck(cdrom);
}

psxrecomp::u8 sectorByte(psxrecomp::u32 lba, size_t byteIndex)
{
    return static_cast<psxrecomp::u8>((lba * 11u + static_cast<psxrecomp::u32>(byteIndex)) & 0xFFu);
}

// Build the little-endian u32 word that readDma() should return for successive
// bytes [base..base+3] of a sector.
psxrecomp::u32 sectorWord(psxrecomp::u32 lba, size_t base)
{
    psxrecomp::u32 w = 0;
    for (size_t s = 0; s < 4u; ++s)
    {
        w |= static_cast<psxrecomp::u32>(sectorByte(lba, base + s)) << (s * 8u);
    }
    return w;
}

} // namespace

int main()
{
    using psxrecomp::runtime::Cdrom;

    PatternDisc disc;

    // -----------------------------------------------------------------------
    // Test 1 — INT1 alone does not make sector data visible.
    //
    // After ReadN produces INT1 the host has not acknowledged the sector yet.
    // DRQSTS must remain 0 and both readData() and readDma() must return zero.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x01u); // LBA=1
        cdrom.writeCommand(0x06u);               // ReadN
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        cdrom.tick(kReadCycles);
        assert(irqType(cdrom) == 0x01u);

        // BFRD has not been written — DRQSTS must be 0.
        assert(!drqsts(cdrom));
        // CPU data port gated by BFRD.
        assert(cdrom.readData() == 0x00u);
        // DMA3 path obeys the same gate.
        assert(cdrom.readDma() == 0x00000000u);

        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Test 2 — Writing BFRD=1 while INT1 is pending raises DRQSTS and makes
    //           sector bytes accessible through readData() and readDma().
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x01u); // LBA=1
        cdrom.writeCommand(0x06u);               // ReadN
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        cdrom.tick(kReadCycles);
        assert(irqType(cdrom) == 0x01u);

        // DRQSTS must be 0 before BFRD is written.
        assert(!drqsts(cdrom));

        enableBufferRead(cdrom);

        // DRQSTS must rise once the host accepts the sector for reading.
        assert(drqsts(cdrom));

        // CPU read returns expected sector data.
        assert(cdrom.readData() == sectorByte(1u, 0u));
        assert(drqsts(cdrom)); // Still set; bytes remain.

        // DMA path returns the next word.
        const psxrecomp::u32 dmaWord = cdrom.readDma();
        assert(dmaWord == sectorWord(1u, 1u));
        assert(drqsts(cdrom)); // Still set.

        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Test 3 — HCLRCTL acknowledges the interrupt without discarding the
    //           accepted data.  DRQSTS must survive the HCLRCTL write and
    //           all sector bytes must remain readable until they are consumed.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x01u); // LBA=1
        cdrom.writeCommand(0x06u);               // ReadN
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        cdrom.tick(kReadCycles);
        assert(irqType(cdrom) == 0x01u);

        enableBufferRead(cdrom);
        assert(drqsts(cdrom));

        // Acknowledge INT1 via HCLRCTL — this must not destroy the data window.
        ack(cdrom);
        assert(irqType(cdrom) == 0x00u);

        // DRQSTS and data must still be accessible post-ACK.
        assert(drqsts(cdrom));
        assert(cdrom.readData() == sectorByte(1u, 0u));

        // Remaining bytes must be coherent (BFRD is still set; FIFO intact).
        for (size_t i = 1u; i < 2048u; ++i)
        {
            assert(cdrom.readData() == sectorByte(1u, i));
        }
        assert(!drqsts(cdrom)); // Fully drained.
    }

    // -----------------------------------------------------------------------
    // Test 4 — Draining the sector clears DRQSTS only once the last byte is
    //           consumed.  Overreads return pad bytes (not fresh sector data)
    //           and do not resurrect DRQSTS.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x01u); // LBA=1
        cdrom.writeCommand(0x06u);               // ReadN
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        cdrom.tick(kReadCycles);
        assert(irqType(cdrom) == 0x01u);
        enableBufferRead(cdrom);
        assert(drqsts(cdrom));

        // Drain all 2048 bytes.
        for (size_t i = 0u; i < 2048u; ++i)
        {
            assert(cdrom.readData() == sectorByte(1u, i));
        }

        // DRQSTS must clear immediately after the last byte is consumed.
        assert(!drqsts(cdrom));

        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Test 5 — DMA3 obeys the same host-ready gate as the CPU data port.
    //
    // A DMA read attempted before BFRD/DRQSTS must return 0 (no data exposed).
    // After BFRD is set the DMA must return the correct sector bytes.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x02u); // LBA=2
        cdrom.writeCommand(0x06u);               // ReadN
        assert(irqType(cdrom) == 0x03u);
        readAndAck(cdrom);

        cdrom.tick(kReadCycles);
        assert(irqType(cdrom) == 0x01u);

        // Before BFRD: DMA3 must be gated (returns 0, DRQSTS=0).
        assert(!drqsts(cdrom));
        assert(cdrom.readDma() == 0x00000000u);

        // After BFRD: DMA3 returns the correct sector bytes.
        enableBufferRead(cdrom);
        assert(drqsts(cdrom));
        assert(cdrom.readDma() == sectorWord(2u, 0u));
        assert(cdrom.readDma() == sectorWord(2u, 4u));

        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Test 6: Clearing BFRD between sectors resets the data-ready signal for
    //         each INT1 independently.  The game must re-arm BFRD for every
    //         sector; a single BFRD write must not span multiple INT1 events.
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

        cdrom.tick(kReadCycles * 2u);
        assert(irqType(cdrom) == 0x01u); // INT1 for LBA=0.

        // Arm BFRD, drain sector 0, clear BFRD, then ack INT1.
        enableBufferRead(cdrom);
        for (size_t i = 0u; i < 2048u; ++i)
        {
            (void)cdrom.readData();
        }
        assert(!drqsts(cdrom));
        disableBufferRead(cdrom);
        readAndAck(cdrom);

        // INT1 for sector 1 becomes visible after the short post-ACK delay.
        cdrom.tick(1u);
        assert(irqType(cdrom) == 0x01u);

        // DRQSTS must be 0 until BFRD is re-armed.
        assert(!drqsts(cdrom));
        assert(cdrom.readDma() == 0x00000000u);

        // Re-arm BFRD for sector 1.
        enableBufferRead(cdrom);
        assert(drqsts(cdrom));
        assert(cdrom.readDma() == sectorWord(1u, 0u));

        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Integration 1 — Scripted Setloc → ReadN canonical sequence.
    //
    // Verify the exact host-side progression documented in PSX-SPX:
    //   INT3 → INT1 → BFRD=1 → DRQSTS=1 → drain sector → DRQSTS=0
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        // Phase 1: Setloc (LBA=3).
        issueSetloc(cdrom, 0x00u, 0x02u, 0x03u);
        assert(irqType(cdrom) == 0x00u);

        // Phase 2: ReadN → INT3 immediately, DRQSTS still 0.
        cdrom.writeCommand(0x06u);
        assert(irqType(cdrom) == 0x03u);
        assert(!drqsts(cdrom));
        readAndAck(cdrom);
        assert(irqType(cdrom) == 0x00u);

        // Phase 3: Waiting — neither INT1 nor data yet.
        cdrom.tick(kReadCycles - 1u);
        assert(irqType(cdrom) == 0x00u);
        assert(!drqsts(cdrom));

        // Phase 4: Sector ready → INT1, DRQSTS still 0 (BFRD not set).
        cdrom.tick(1u);
        assert(irqType(cdrom) == 0x01u);
        assert(!drqsts(cdrom));

        // Phase 5: BFRD=1 → DRQSTS rises.
        enableBufferRead(cdrom);
        assert(drqsts(cdrom));

        // Phase 6: Acknowledge INT1 — data window must survive.
        ack(cdrom);
        assert(irqType(cdrom) == 0x00u);
        assert(drqsts(cdrom));

        // Phase 7: Drain the full sector via CPU readData().
        for (size_t i = 0u; i < 2048u; ++i)
        {
            assert(cdrom.readData() == sectorByte(3u, i));
        }

        // Phase 8: Sector fully consumed → DRQSTS cleared.
        assert(!drqsts(cdrom));
    }

    return 0;
}
