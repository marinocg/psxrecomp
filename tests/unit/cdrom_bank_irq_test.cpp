#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <cassert>
#include <vector>

namespace
{
constexpr psxrecomp::u32 kCdromReadCycles = 451584;

class PatternDisc final : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(psxrecomp::u32 lba, std::span<psxrecomp::u8, 2048> out) override
    {
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = static_cast<psxrecomp::u8>((lba + static_cast<psxrecomp::u32>(i)) & 0xFFu);
        }
        return true;
    }

    psxrecomp::u32 userSectorCount() const override
    {
        return 64;
    }
};

[[maybe_unused]] psxrecomp::u8 irqType(const psxrecomp::runtime::Cdrom& cdrom)
{
    return static_cast<psxrecomp::u8>(cdrom.readInterruptFlags() & 0x07u);
}

void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeInterruptFlags(0x07);
}
} // namespace

int main()
{
    using psxrecomp::runtime::Cdrom;

    PatternDisc disc;

    // --- Test 1: Bank select register (offset 0) ---
    // Writing to offset 0 sets the bank index; reading offset 0 returns it in bits 0-1.
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);

        // Default index is 0.
        assert((cdrom.readReg(0) & 0x03u) == 0x00);

        // Set index 1.
        cdrom.writeReg(0, 0x01);
        assert((cdrom.readReg(0) & 0x03u) == 0x01);

        // Set index 3.
        cdrom.writeReg(0, 0x03);
        assert((cdrom.readReg(0) & 0x03u) == 0x03);

        // Only bits 0-1 are used.
        cdrom.writeReg(0, 0x07);
        assert((cdrom.readReg(0) & 0x03u) == 0x03);

        // Index 2.
        cdrom.writeReg(0, 0x02);
        assert((cdrom.readReg(0) & 0x03u) == 0x02);
    }

    // --- Test 2: Offset 3 reads vary by bank ---
    // Index 0 / 2 → Interrupt Enable, Index 1 / 3 → Interrupt Flags.
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);

        // Index 0: offset 3 reads Interrupt Enable.
        cdrom.writeReg(0, 0x00);
        const psxrecomp::u8 ie0 = cdrom.readReg(3);
        assert((ie0 & 0x1Fu) == 0x1Fu);

        // Index 2: offset 3 also reads Interrupt Enable.
        cdrom.writeReg(0, 0x02);
        const psxrecomp::u8 ie2 = cdrom.readReg(3);
        assert((ie2 & 0x1Fu) == 0x1Fu);

        // Index 1: offset 3 reads Interrupt Flags.
        cdrom.writeReg(0, 0x01);
        const psxrecomp::u8 ifl = cdrom.readReg(3);
        // No active IRQ → flags bits 0-2 are 0, bits 5-7 are 0xE0.
        assert((ifl & 0x07u) == 0x00u);
        assert((ifl & 0xE0u) == 0xE0u);

        // Index 3: offset 3 also reads Interrupt Flags.
        cdrom.writeReg(0, 0x03);
        const psxrecomp::u8 ifl3 = cdrom.readReg(3);
        assert((ifl3 & 0x07u) == 0x00u);
        assert((ifl3 & 0xE0u) == 0xE0u);
    }

    // --- Test 3: Audio volume writes (index 2/3) must not crash or corrupt state ---
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);

        // Index 2, offset 2: Left-CD to Left-SPU Volume (no-op).
        cdrom.writeReg(0, 0x02);
        cdrom.writeReg(2, 0x80);

        // Index 3, offset 2: Right-CD to Left-SPU Volume (no-op).
        cdrom.writeReg(0, 0x03);
        cdrom.writeReg(2, 0x80);

        // Index 2, offset 3: Left-CD to Right-SPU Volume (no-op).
        cdrom.writeReg(0, 0x02);
        cdrom.writeReg(3, 0x40);

        // Index 3, offset 3: Apply Volume Changes (no-op).
        cdrom.writeReg(0, 0x03);
        cdrom.writeReg(3, 0x20);

        // Verify CDROM is still functional after volume writes.
        cdrom.writeReg(0, 0x00);
        cdrom.writeCommand(0x01); // Getstat
        assert(irqType(cdrom) == 0x03);
        (void)cdrom.readResponse();
        ack(cdrom);
    }

    // --- Test 4: ReadN INT3 has single stat byte; INT1 promotes after drain+ack ---
    // This is the core test for the Reversi 2 stall fix.
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);

        // Setloc to LBA=0.
        cdrom.writeParam(0x00);
        cdrom.writeParam(0x02);
        cdrom.writeParam(0x00);
        cdrom.writeCommand(0x02);
        assert(irqType(cdrom) == 0x03);
        (void)cdrom.readResponse();
        ack(cdrom);

        // Issue ReadN.
        cdrom.writeCommand(0x06);
        assert(irqType(cdrom) == 0x03);

        // INT3 response: exactly ONE stat byte (seek+motor = 0x42).
        const psxrecomp::u8 stat = cdrom.readResponse();
        assert(stat == 0x42);

        // Response FIFO should now be empty — RSLRRDY must clear.
        assert((cdrom.readStatus() & (1u << 5)) == 0u);

        // Acknowledge INT3.
        ack(cdrom);

        // After acking INT3, no IRQ is active yet (tick hasn't fired).
        assert(irqType(cdrom) == 0x00);

        // Advance time so the sector read completes → INT1 should publish.
        cdrom.tick(kCdromReadCycles);

        // INT1 must be promoted now that INT3 was fully cleared.
        assert(irqType(cdrom) == 0x01);

        // Read INT1 stat response.
        const psxrecomp::u8 dataStat = cdrom.readResponse();
        assert(dataStat == 0x22); // read + motor

        ack(cdrom);
        assert(irqType(cdrom) == 0x00);
    }

    // --- Test 5: Multi-sector ReadN with repeated INT1 promotion ---
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);

        cdrom.writeParam(0x00);
        cdrom.writeParam(0x02);
        cdrom.writeParam(0x00);
        cdrom.writeCommand(0x02);
        assert(irqType(cdrom) == 0x03);
        (void)cdrom.readResponse();
        ack(cdrom);

        cdrom.writeCommand(0x06); // ReadN
        assert(irqType(cdrom) == 0x03);
        (void)cdrom.readResponse();
        ack(cdrom);

        // Read 3 consecutive sectors.
        for (int sector = 0; sector < 3; ++sector)
        {
            cdrom.tick(kCdromReadCycles);
            assert(irqType(cdrom) == 0x01);

            // Enable BFRD and verify data is readable.
            cdrom.writeReg(0, 0x00);
            cdrom.writeReg(3, 0x80);
            const psxrecomp::u8 firstByte = cdrom.readData();
            (void)firstByte; // Content correctness tested elsewhere.

            // Drain responses and ack.
            while ((cdrom.readStatus() & (1u << 5)) != 0u)
            {
                (void)cdrom.readResponse();
            }
            ack(cdrom);

            // After acking INT1, no IRQ until next tick.
            assert(irqType(cdrom) == 0x00);
        }
    }

    // --- Test 6: ReadS has same single-stat-byte INT3 behavior ---
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);

        cdrom.writeParam(0x00);
        cdrom.writeParam(0x02);
        cdrom.writeParam(0x00);
        cdrom.writeCommand(0x02);
        assert(irqType(cdrom) == 0x03);
        (void)cdrom.readResponse();
        ack(cdrom);

        cdrom.writeCommand(0x1B); // ReadS
        assert(irqType(cdrom) == 0x03);

        // Single stat byte.
        (void)cdrom.readResponse();
        assert((cdrom.readStatus() & (1u << 5)) == 0u);
        ack(cdrom);

        cdrom.tick(kCdromReadCycles);
        assert(irqType(cdrom) == 0x01);
        (void)cdrom.readResponse();
        ack(cdrom);
    }

    return 0;
}
