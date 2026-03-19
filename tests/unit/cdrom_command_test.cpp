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
    explicit StaticDisc(Region region = Region::NorthAmerica) : m_region(region) {}

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

    Region region() const override
    {
        return m_region;
    }

  private:
    Region m_region = Region::NorthAmerica;
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
    using psxrecomp::runtime::Disc;

    const auto assertGetIdRegion = [](Disc& testDisc, std::initializer_list<psxrecomp::u8> expected)
    {
        Cdrom testCdrom;
        testCdrom.reset();
        testCdrom.setDiscBackend(&testDisc);
        testCdrom.writeInterruptEnable(0x1F);
        testCdrom.writeCommand(0x1A);
        assert(irqType(testCdrom) == 0x03);
        assertResponse(testCdrom, {0x00});
        ack(testCdrom);
        assert(irqType(testCdrom) == 0x02);
        assertResponse(testCdrom, expected);
        ack(testCdrom);
    };

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
    assertResponse(cdrom, {0x42}); // PSX-SPX: ReadN INT3 has single stat byte
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
    assertResponse(cdrom, {0x42}); // PSX-SPX: ReadS INT3 has single stat byte
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

    StaticDisc jpDisc(Disc::Region::Japan);
    assertGetIdRegion(jpDisc, {0x00, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'I'});

    StaticDisc euDisc(Disc::Region::Europe);
    assertGetIdRegion(euDisc, {0x00, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'E'});

    // ACKing INT2 before draining its response drains the unread result bytes
    // (PSX-SPX HCLRCTL behavior), so the FIFO is empty afterwards.
    cdrom.writeCommand(0x1A); // GetID (disc present)
    assert(irqType(cdrom) == 0x03);
    assertResponse(cdrom, {0x00});
    ack(cdrom);
    assert(irqType(cdrom) == 0x02);

    cdrom.writeInterruptFlags(0x02); // ACK INT2 before reading its response.
    assert(irqType(cdrom) == 0x00);
    assert((cdrom.readStatus() & (1u << 5)) == 0u);
    assert(cdrom.readResponse() == 0x00);

    return 0;
}
