#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <array>
#include <cassert>
#include <vector>

namespace
{
constexpr psxrecomp::u32 kCdromReadCycles = 451584;

class StaticDisc final : public psxrecomp::runtime::Disc
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
        return 1024;
    }
};

[[maybe_unused]] psxrecomp::u8 irqType(const psxrecomp::runtime::Cdrom& cdrom)
{
    return static_cast<psxrecomp::u8>(cdrom.readInterruptFlags() & 0x07u);
}

std::vector<psxrecomp::u8> readResponse(psxrecomp::runtime::Cdrom& cdrom, size_t count)
{
    std::vector<psxrecomp::u8> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        out.push_back(cdrom.readResponse());
    }
    return out;
}

void assertResponse(psxrecomp::runtime::Cdrom& cdrom, std::initializer_list<psxrecomp::u8> expected)
{
    const std::vector<psxrecomp::u8> got = readResponse(cdrom, expected.size());
    assert(got.size() == expected.size());
    size_t i = 0;
    for ([[maybe_unused]] psxrecomp::u8 value : expected)
    {
        assert(got[i] == value);
        ++i;
    }
}

void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeInterruptFlags(0x07);
}
} // namespace

int main()
{
    using psxrecomp::runtime::Cdrom;

    Cdrom cdrom;
    StaticDisc disc;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    cdrom.writeCommand(0x0A); // Init
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x02});
    ack(cdrom);
    assert(irqType(cdrom) == 0x02);
    cdrom.writeCommand(0x0C); // Demute must wait until Init INT2 is acknowledged.
    assert(irqType(cdrom) == 0x02);
    assertResponse(cdrom, {0x02});
    ack(cdrom);
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x02});
    ack(cdrom);

    cdrom.writeCommand(0x01); // Getstat
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x02});
    ack(cdrom);

    // INT3 delivery must honor the INT3 enable bit (bit2) and ACK must honor
    // one-hot ACK bits (bit2 acknowledges current INT3).
    cdrom.writeInterruptEnable(0x04);
    cdrom.writeCommand(0x01); // Getstat -> INT3
    assert(irqType(cdrom) == 0x03);
    assert(cdrom.hasIrqRequest());
    cdrom.writeInterruptFlags(0x01); // Wrong ACK bit for INT3.
    assert(irqType(cdrom) == 0x03);
    assert(cdrom.hasIrqRequest());
    cdrom.writeInterruptFlags(0x04); // Correct ACK bit for INT3.
    assert(irqType(cdrom) == 0x00);
    assert(!cdrom.hasIrqRequest());
    // The controller IRQ is cleared, but software should still drain the
    // current response byte before issuing a new command.
    (void)cdrom.readResponse();
    assert(irqType(cdrom) == 0x00);
    cdrom.writeInterruptEnable(0x1F);

    cdrom.writeParam(0x00);
    cdrom.writeParam(0x02);
    cdrom.writeParam(0x00);
    cdrom.writeCommand(0x02); // Setloc 00:02:00
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x02});
    ack(cdrom);

    cdrom.writeCommand(0x15); // SeekL
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x42});
    ack(cdrom);
    assert(irqType(cdrom) == 0x02);
    assertResponse(cdrom, {0x02});
    ack(cdrom);

    cdrom.writeParam(0x40);
    cdrom.writeCommand(0x0E); // Setmode (XA on)
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x02});
    ack(cdrom);

    cdrom.writeParam(0x01);
    cdrom.writeParam(0x02);
    cdrom.writeCommand(0x0D); // Setfilter(file=1, channel=2)
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x02});
    ack(cdrom);

    cdrom.writeCommand(0x06); // ReadN
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x42});
    ack(cdrom);
    cdrom.tick(kCdromReadCycles);
    assert(irqType(cdrom) == 0x01);
    assertResponse(cdrom, {0x22});
    ack(cdrom);

    cdrom.writeCommand(0x09); // Pause
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x22});
    ack(cdrom);
    assert(irqType(cdrom) == 0x02);
    assertResponse(cdrom, {0x02});
    ack(cdrom);

    cdrom.writeCommand(0x1B); // ReadS
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x42});
    ack(cdrom);
    cdrom.tick(kCdromReadCycles);
    assert(irqType(cdrom) == 0x01);
    assertResponse(cdrom, {0x22});
    ack(cdrom);

    cdrom.writeCommand(0x08); // Stop
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x02});
    ack(cdrom);
    assert(irqType(cdrom) == 0x02);
    assertResponse(cdrom, {0x00});
    ack(cdrom);

    cdrom.writeParam(0x00);
    cdrom.writeParam(0x02);
    cdrom.writeParam(0x00);
    cdrom.writeCommand(0x02); // Setloc 00:02:00
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x00});
    ack(cdrom);

    cdrom.writeCommand(0x10); // GetlocL
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00});
    ack(cdrom);

    cdrom.writeCommand(0x11); // GetlocP
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00});
    ack(cdrom);

    cdrom.writeCommand(0x13); // GetTN
    assert(irqType(cdrom) == 0x03);
    {
        const auto tn = readResponse(cdrom, 3);
        assert(tn[0] == 0x00);
        assert(tn[1] == 0x01);
        assert(tn[2] == 0x01);
    }
    ack(cdrom);

    cdrom.writeParam(0x01);
    cdrom.writeCommand(0x14); // GetTD(track 1)
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x00, 0x00, 0x02});
    ack(cdrom);

    cdrom.writeParam(0x00);
    cdrom.writeCommand(0x14); // GetTD(lead-out)
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x00, 0x00, 0x15});
    ack(cdrom);

    cdrom.writeCommand(0x1A); // GetID (disc present)
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x00});
    ack(cdrom);
    assert(irqType(cdrom) == 0x02);
    assertResponse(cdrom, {0x00, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'A'});
    ack(cdrom);

    // ACKing INT2 before draining its response preserves that response byte.
    // The next command is not presented until software consumes that byte.
    cdrom.writeCommand(0x1A); // GetID (disc present)
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x00});
    ack(cdrom);
    assert(irqType(cdrom) == 0x02);

    cdrom.writeInterruptFlags(0x02); // ACK INT2 before reading its response.
    assert(irqType(cdrom) == 0x00);
    assert(cdrom.readResponse() == 0x00);

    return 0;
}
