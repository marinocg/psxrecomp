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
            out[i] =
                static_cast<psxrecomp::u8>((lba * 3u + static_cast<psxrecomp::u32>(i)) & 0xFFu);
        }
        return true;
    }

    psxrecomp::u32 userSectorCount() const override
    {
        return 2048;
    }
};

psxrecomp::u8 irqType(const psxrecomp::runtime::Cdrom& cdrom)
{
    return static_cast<psxrecomp::u8>(cdrom.readInterruptFlags() & 0x07u);
}

void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeInterruptFlags(0x07u);
}

void drainIrqs(psxrecomp::runtime::Cdrom& cdrom)
{
    for (int i = 0; i < 16 && irqType(cdrom) != 0u; ++i)
    {
        (void)cdrom.readResponse();
        ack(cdrom);
    }
    assert(irqType(cdrom) == 0u);
}
} // namespace

int main()
{
    using psxrecomp::runtime::Cdrom;

    PatternDisc disc;

    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);
        cdrom.writeReg(0, 0x02u);

        cdrom.writeCommand(0x0A); // Init: INT3 visible, INT2 queued.
        cdrom.writeParam(0x01);   // Leave one command parameter queued.

        const std::vector<psxrecomp::u8> saved = cdrom.serializeState();

        cdrom.reset();
        assert(cdrom.deserializeState(saved));

        assert((cdrom.readStatus() & 0x03u) == 0x02u);
        assert(cdrom.hasIrqRequest());
        assert(irqType(cdrom) == 0x03u);
        assert(cdrom.readResponse() == 0x02u);
        ack(cdrom);
        assert(irqType(cdrom) == 0x02u);
        assert(cdrom.readResponse() == 0x02u);
        ack(cdrom);
        assert(irqType(cdrom) == 0x00u);

        cdrom.writeCommand(0x14); // GetTD consumes queued track parameter (0x01).
        assert(irqType(cdrom) == 0x03u);
        assert(cdrom.readResponse() == 0x02u);
        assert(cdrom.readResponse() == 0x00u);
        assert(cdrom.readResponse() == 0x02u);
        ack(cdrom);
    }

    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1Fu);

        cdrom.writeParam(0x20u); // Whole-sector mode.
        cdrom.writeCommand(0x0E);
        assert(irqType(cdrom) == 0x03u);
        (void)cdrom.readResponse();
        ack(cdrom);

        cdrom.writeParam(0x00u);
        cdrom.writeParam(0x02u);
        cdrom.writeParam(0x00u);
        cdrom.writeCommand(0x02); // Setloc 00:02:00 (LBA 0)
        assert(irqType(cdrom) == 0x03u);
        (void)cdrom.readResponse();
        ack(cdrom);

        cdrom.writeCommand(0x06); // ReadN
        assert(irqType(cdrom) == 0x03u);
        (void)cdrom.readResponse();
        ack(cdrom);

        cdrom.tick(kCdromReadCycles * 3u);
        for (int i = 0; i < 5; ++i)
        {
            (void)cdrom.readData();
        }

        drainIrqs(cdrom);
        cdrom.writeCommand(0x11); // GetlocP
        assert(irqType(cdrom) == 0x03u);
        std::vector<psxrecomp::u8> expectedLoc(8);
        for (psxrecomp::u8& value : expectedLoc)
        {
            value = cdrom.readResponse();
        }
        ack(cdrom);

        const std::vector<psxrecomp::u8> saved = cdrom.serializeState();
        cdrom.reset();
        assert(cdrom.deserializeState(saved));

        const std::vector<psxrecomp::u8> expected = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2};
        for (psxrecomp::u8 value : expected)
        {
            assert(cdrom.readData() == value);
        }

        drainIrqs(cdrom);
        cdrom.writeCommand(0x11); // GetlocP
        assert(irqType(cdrom) == 0x03u);
        for (psxrecomp::u8 expectedValue : expectedLoc)
        {
            assert(cdrom.readResponse() == expectedValue);
        }
        ack(cdrom);
    }

    return 0;
}
