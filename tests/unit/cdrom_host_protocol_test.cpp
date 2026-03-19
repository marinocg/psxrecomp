// Tests for the CDROM host-side ReadN protocol:
//   INT3 (command ack) -> INT1 (data-ready) queue ordering,
//   HCLRCTL (interrupt acknowledge + queue advance), and
//   data-ready gating through REQUEST_ENABLE_BUFFER_READ (BFRD).
//
// PSX-SPX reference:
//   http://problemkaputt.de/psx-spx.htm#cdromcontrollercommandsummary

#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
constexpr psxrecomp::u32 kReadCycles = 451584u; // Single-speed sector cadence.

// Minimal disc: sector N fills with (N * 7 + byte_index) & 0xFF.
class PatternDisc final : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(psxrecomp::u32 lba, std::span<psxrecomp::u8, 2048> out) override
    {
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] =
                static_cast<psxrecomp::u8>((lba * 7u + static_cast<psxrecomp::u32>(i)) & 0xFFu);
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

// Write HCLRCTL=0x07: acknowledge the current interrupt and advance queue.
void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeInterruptFlags(0x07u);
}

// Enable the data FIFO (BFRD bit in the REQUEST register, bank 0 offset 3).
void enableBufferRead(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0u, 0u); // select bank 0
    cdrom.writeReg(3u, 0x80u);
}

// Disable the data FIFO (clear BFRD).
void disableBufferRead(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0u, 0u); // select bank 0
    cdrom.writeReg(3u, 0x00u);
}

// Drain the response FIFO then acknowledge the current interrupt (read-then-ack).
void readAndAck(psxrecomp::runtime::Cdrom& cdrom)
{
    while ((cdrom.readStatus() & (1u << 5u)) != 0u)
    {
        (void)cdrom.readResponse();
    }
    ack(cdrom);
}

// Issue a SetLoc command (writes MM/SS/FF BCD params then command 0x02).
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
    return static_cast<psxrecomp::u8>((lba * 7u + static_cast<psxrecomp::u32>(byteIndex)) & 0xFFu);
}

} // namespace

int main()
{
    using psxrecomp::runtime::Cdrom;

    PatternDisc disc;

    // -----------------------------------------------------------------------
    // Test 1: ReadN issues INT3 first (command ack), not INT1.
    // PSX-SPX: "CdlReadN - Read with Retry: INT3(stat), INT1(stat), INT1(stat)..."
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x01u); // LBA=1

        cdrom.writeCommand(0x06u); // ReadN
        // INT3 must be immediately visible (command acknowledged).
        assert(irqType(cdrom) == 0x03u);
        // No INT1 before the physical read delay has elapsed.
        readAndAck(cdrom);
        assert(irqType(cdrom) == 0x00u);
    }

    // -----------------------------------------------------------------------
    // Test 2: After INT3 is acknowledged, INT1 fires once the sector is ready.
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

        // Sector not yet ready.
        cdrom.tick(kReadCycles - 1u);
        assert(irqType(cdrom) == 0x00u);

        // Exactly one more cycle — INT1 fires.
        cdrom.tick(1u);
        assert(irqType(cdrom) == 0x01u);
    }

    // -----------------------------------------------------------------------
    // Test 3: Sector data is NOT visible before the game sets BFRD.
    //
    // PSX-SPX: The game must write REQUEST_ENABLE_BUFFER_READ (bit 7 of
    // 1F801803h bank-0) before sector bytes are available.  Emitting INT1
    // must NOT auto-enable BFRD or pre-fill the data FIFO.
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

        // BFRD has not been set yet — readData() must return 0.
        assert(cdrom.readData() == 0x00u);

        // Now set BFRD; data becomes visible.
        enableBufferRead(cdrom);
        assert(cdrom.readData() == sectorByte(1u, 0u));
        assert(cdrom.readData() == sectorByte(1u, 1u));

        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Test 4: HCLRCTL written before response is drained still advances the
    //         IRQ queue (BIOS ack-before-read pattern).
    //
    // PSX-SPX: "the BIOS does it in reverse order: wait for an IRQ, write
    // 07h to reset the flags, then read the response."
    // The response bytes are buffered; the queue must not be permanently
    // blocked by unread bytes from the previous interrupt.
    // -----------------------------------------------------------------------
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        issueSetloc(cdrom, 0x00u, 0x02u, 0x01u); // LBA=1
        cdrom.writeCommand(0x06u);               // ReadN
        assert(irqType(cdrom) == 0x03u);

        // Acknowledge WITHOUT reading the INT3 response byte first.
        ack(cdrom);

        // The queue should now be unblocked; INT1 must fire after the sector.
        cdrom.tick(kReadCycles);
        assert(irqType(cdrom) == 0x01u);

        enableBufferRead(cdrom);
        assert(cdrom.readData() == sectorByte(1u, 0u));
        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Test 5: Later sectors stay buffered, but the host only sees one active
    // INT1 at a time.
    //
    // Two ticks buffer two sectors; only the first INT1 should be visible
    // until the game acknowledges it.
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

        // Two read cycles: sectors 0 and 1 both become ready.
        cdrom.tick(kReadCycles * 2u);

        // First INT1 (LBA=0) is visible.
        assert(irqType(cdrom) == 0x01u);
        assert(cdrom.debugSnapshot().pendingIrqCount == 0u);

        // Enable BFRD, read first byte of LBA=0, then disable BFRD.
        enableBufferRead(cdrom);
        assert(cdrom.readData() == sectorByte(0u, 0u));
        disableBufferRead(cdrom); // clear FIFO; game must re-enable for next sector
        readAndAck(cdrom);
        cdrom.tick(1u);

        // After a short post-ACK delay, the buffered INT1 for LBA=1 surfaces.
        assert(irqType(cdrom) == 0x01u);
        assert(cdrom.debugSnapshot().pendingIrqCount == 0u);

        // The game must write BFRD again for each new sector.
        enableBufferRead(cdrom);
        assert(cdrom.readData() == sectorByte(1u, 0u));
        readAndAck(cdrom);

        // No more pending interrupts.
        assert(irqType(cdrom) == 0x00u);
    }

    // -----------------------------------------------------------------------
    // Test 6: Buffered sectors must not accumulate a software queue of INT1s.
    //
    // PSX-SPX notes that streamed reads can overrun and skip sectors rather
    // than building an unbounded interrupt backlog. The runtime should keep at
    // most one host-visible INT1 pending while later sectors remain buffered.
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

        cdrom.tick(kReadCycles * 6u);
        assert(irqType(cdrom) == 0x01u);
        assert(cdrom.debugSnapshot().pendingIrqCount == 0u);

        enableBufferRead(cdrom);
        (void)cdrom.readData();
        disableBufferRead(cdrom);
        readAndAck(cdrom);
        cdrom.tick(1u);

        assert(irqType(cdrom) == 0x01u);
        assert(cdrom.debugSnapshot().pendingIrqCount == 0u);
    }

    // -----------------------------------------------------------------------
    // Test 7: Clearing BFRD hides sector data; re-enabling loads the same
    //         active sector (not the next one) on a fresh 0->1 transition.
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

        // Enable, read one byte, disable.
        enableBufferRead(cdrom);
        assert(cdrom.readData() == sectorByte(2u, 0u));
        disableBufferRead(cdrom);

        // FIFO cleared by BFRD falling edge — readData returns 0.
        assert(cdrom.readData() == 0x00u);

        // Re-enable: the same active sector (LBA=2) is reloaded from the start.
        enableBufferRead(cdrom);
        assert(cdrom.readData() == sectorByte(2u, 0u));
        assert(cdrom.readData() == sectorByte(2u, 1u));

        readAndAck(cdrom);
    }

    // -----------------------------------------------------------------------
    // Test 8: INT1 response stat byte is readable after BFRD is set.
    //         The stat byte must come from the response FIFO (offset 1 bank 1),
    //         not the data FIFO (offset 2).
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

        // Response byte should reflect "reading + motor on" (0x22).
        const psxrecomp::u8 stat = cdrom.readResponse();
        assert(stat == 0x22u);

        // Data FIFO still empty until BFRD is set.
        assert(cdrom.readData() == 0x00u);

        enableBufferRead(cdrom);
        assert(cdrom.readData() == sectorByte(1u, 0u));

        ack(cdrom);
    }

    return 0;
}
