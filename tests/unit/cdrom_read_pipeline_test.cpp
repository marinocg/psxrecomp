#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <array>
#include <cassert>
#include <vector>

namespace
{
constexpr psxrecomp::u32 kCdromReadCycles = 451584;

psxrecomp::u8 userByte(psxrecomp::u32 lba, size_t offset)
{
    return static_cast<psxrecomp::u8>((lba * 31u + static_cast<psxrecomp::u32>(offset)) & 0xFFu);
}

class PatternDisc final : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(psxrecomp::u32 lba, std::span<psxrecomp::u8, 2048> out) override
    {
        if (lba >= 8u)
        {
            return false;
        }
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = userByte(lba, i);
        }
        return true;
    }

    bool readRawSector2352(psxrecomp::u32 lba, std::span<psxrecomp::u8, 2352> out) override
    {
        if (lba >= 8u)
        {
            return false;
        }
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] =
                static_cast<psxrecomp::u8>((0xA0u + lba + static_cast<psxrecomp::u32>(i)) & 0xFFu);
        }
        for (size_t i = 0; i < 2048; ++i)
        {
            out[24 + i] = userByte(lba, i);
        }
        return true;
    }

    psxrecomp::u32 userSectorCount() const override
    {
        return 8;
    }
};

std::array<psxrecomp::u8, 2352> makeXaSector(psxrecomp::u8 file, psxrecomp::u8 channel,
                                             psxrecomp::u8 submode, psxrecomp::u8 payloadBase,
                                             bool duplicateSubheader = true)
{
    std::array<psxrecomp::u8, 2352> raw{};
    raw[15] = 0x02; // Mode2
    raw[16] = file;
    raw[17] = channel;
    raw[18] = submode;
    raw[19] = 0x00;
    raw[20] = duplicateSubheader ? raw[16] : static_cast<psxrecomp::u8>(raw[16] ^ 0xFFu);
    raw[21] = raw[17];
    raw[22] = raw[18];
    raw[23] = raw[19];
    for (size_t i = 0; i < 2324; ++i)
    {
        raw[24 + i] = static_cast<psxrecomp::u8>(payloadBase + static_cast<psxrecomp::u8>(i));
    }
    return raw;
}

class XaPatternDisc final : public psxrecomp::runtime::Disc
{
  public:
    XaPatternDisc()
    {
        // Sector 0: XA ADPCM-ish sector (form2 + realtime + audio), file=1 channel=2.
        m_rawSectors.push_back(makeXaSector(0x01, 0x02, 0x64, 0x10));
        // Sector 1: XA ADPCM-ish sector with different file/channel; should be filtered out.
        m_rawSectors.push_back(makeXaSector(0x03, 0x04, 0x64, 0x80));
        // Sector 2: XA ADPCM-ish sector matching file/channel; should pass with filter.
        m_rawSectors.push_back(makeXaSector(0x01, 0x02, 0x64, 0xC0));
        // Sector 3: Malformed duplicated subheader; should not be treated as valid XA form2.
        m_rawSectors.push_back(makeXaSector(0x01, 0x02, 0x64, 0x55, false));
    }

    bool readUserSector(psxrecomp::u32 lba, std::span<psxrecomp::u8, 2048> out) override
    {
        if (lba >= m_rawSectors.size())
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

    bool readRawSector2352(psxrecomp::u32 lba, std::span<psxrecomp::u8, 2352> out) override
    {
        if (lba >= m_rawSectors.size())
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

    psxrecomp::u32 userSectorCount() const override
    {
        return static_cast<psxrecomp::u32>(m_rawSectors.size());
    }

  private:
    std::vector<std::array<psxrecomp::u8, 2352>> m_rawSectors;
};

psxrecomp::u8 irqType(const psxrecomp::runtime::Cdrom& cdrom)
{
    return static_cast<psxrecomp::u8>(cdrom.readInterruptFlags() & 0x07u);
}

void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeInterruptFlags(0x07);
}

void assertResponse(psxrecomp::runtime::Cdrom& cdrom, std::initializer_list<psxrecomp::u8> expected)
{
    for (psxrecomp::u8 value : expected)
    {
        assert(cdrom.readResponse() == value);
    }
}

void issueSetloc(psxrecomp::runtime::Cdrom& cdrom, psxrecomp::u8 mm, psxrecomp::u8 ss,
                 psxrecomp::u8 ff)
{
    cdrom.writeParam(mm);
    cdrom.writeParam(ss);
    cdrom.writeParam(ff);
    cdrom.writeCommand(0x02);
    assert(irqType(cdrom) == 0x03);
    ack(cdrom);
}

void issueReadN(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeCommand(0x06);
    assert(irqType(cdrom) == 0x03);
    ack(cdrom);
}
} // namespace

int main()
{
    using psxrecomp::runtime::Cdrom;

    PatternDisc disc;

    // Setloc MSF->LBA mapping, cadence, and data register availability by index.
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);

        issueSetloc(cdrom, 0x00, 0x02, 0x01); // LBA=1
        issueReadN(cdrom);

        cdrom.tick(kCdromReadCycles - 1);
        assert(irqType(cdrom) == 0x00);
        cdrom.tick(1);
        assert(irqType(cdrom) == 0x01);

        cdrom.writeReg(0, 0);
        assert(cdrom.readReg(2) == userByte(1, 0));
        cdrom.writeReg(0, 3);
        assert(cdrom.readReg(2) == userByte(1, 1));
        cdrom.writeReg(0, 0);
        for (size_t i = 2; i < 2048; ++i)
        {
            assert(cdrom.readData() == userByte(1, i));
        }
        ack(cdrom);

        cdrom.tick(kCdromReadCycles);
        assert(irqType(cdrom) == 0x01);
        assert(cdrom.readData() == userByte(2, 0));
        ack(cdrom);
    }

    // Setmode bit5 (sector size) must change data bytes exposed through RDDATA.
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);
        cdrom.writeParam(0x20); // sector size=2340 mode
        cdrom.writeCommand(0x0E);
        assert(irqType(cdrom) == 0x03);
        ack(cdrom);

        issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA=0
        issueReadN(cdrom);
        cdrom.tick(kCdromReadCycles);
        assert(irqType(cdrom) == 0x01);
        assert(cdrom.readData() == static_cast<psxrecomp::u8>((0xA0u + 12u) & 0xFFu));
        ack(cdrom);
    }

    // XA mode should expose XA payload from validated form2 sectors.
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);
        cdrom.writeParam(0x40); // XA streaming model: skip 24-byte header
        cdrom.writeCommand(0x0E);
        assert(irqType(cdrom) == 0x03);
        ack(cdrom);

        issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA=0
        issueReadN(cdrom);
        cdrom.tick(kCdromReadCycles);
        assert(irqType(cdrom) == 0x01);
        assert(cdrom.readData() == userByte(0, 0));
        ack(cdrom);
    }

    // XA Setfilter should only surface matching XA sectors when enabled.
    {
        XaPatternDisc xaDisc;
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&xaDisc);
        cdrom.writeInterruptEnable(0x1F);

        cdrom.writeParam(0x48); // XA streaming + XA filter enable
        cdrom.writeCommand(0x0E);
        assert(irqType(cdrom) == 0x03);
        ack(cdrom);

        cdrom.writeParam(0x01); // file
        cdrom.writeParam(0x02); // channel
        cdrom.writeCommand(0x0D);
        assert(irqType(cdrom) == 0x03);
        ack(cdrom);

        issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA=0
        issueReadN(cdrom);

        cdrom.tick(kCdromReadCycles);
        assert(irqType(cdrom) == 0x01);
        assert(cdrom.readData() == 0x10);
        assert(cdrom.readData() == 0x11);
        for (size_t i = 2; i < 2324; ++i)
        {
            (void)cdrom.readData();
        }
        ack(cdrom);

        cdrom.tick(kCdromReadCycles);
        assert(irqType(cdrom) == 0x01);
        // Sector1 is filtered out; stream should advance to matching sector2.
        assert(cdrom.readData() == 0xC0);
        assert(cdrom.readData() == 0xC1);
        ack(cdrom);
    }

    // Invalid XA subheader should not apply XA-form2 payload path.
    {
        XaPatternDisc xaDisc;
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&xaDisc);
        cdrom.writeInterruptEnable(0x1F);

        cdrom.writeParam(0x40); // XA streaming
        cdrom.writeCommand(0x0E);
        assert(irqType(cdrom) == 0x03);
        ack(cdrom);

        issueSetloc(cdrom, 0x00, 0x02, 0x03); // LBA=3 (malformed duplicated subheader)
        issueReadN(cdrom);
        cdrom.tick(kCdromReadCycles);
        assert(irqType(cdrom) == 0x01);
        // Falls back to regular 2048-byte user payload beginning at raw[24].
        assert(cdrom.readData() == 0x55);
        assert(cdrom.readData() == 0x56);
        ack(cdrom);
    }

    // Data FIFO overread should return repeat byte (0x800-8 in 2048-byte mode).
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);

        issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA=0
        issueReadN(cdrom);
        cdrom.tick(kCdromReadCycles);
        assert(irqType(cdrom) == 0x01);
        for (size_t i = 0; i < 2048; ++i)
        {
            (void)cdrom.readData();
        }
        assert(cdrom.readData() == userByte(0, 2040));
        ack(cdrom);
    }

    // Pause + new Setloc/ReadN must not leak stale prefetched sector bytes.
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);

        issueSetloc(cdrom, 0x00, 0x02, 0x01); // LBA=1
        issueReadN(cdrom);
        cdrom.tick(kCdromReadCycles);
        assert(irqType(cdrom) == 0x01);
        assertResponse(cdrom, {0x22});
        ack(cdrom);

        cdrom.writeCommand(0x09); // Pause without draining data FIFO.
        assert(irqType(cdrom) == 0x03);
        assertResponse(cdrom, {0x22});
        ack(cdrom);
        assert(irqType(cdrom) == 0x02);
        assertResponse(cdrom, {0x02});
        ack(cdrom);

        issueSetloc(cdrom, 0x00, 0x02, 0x03); // LBA=3
        issueReadN(cdrom);
        cdrom.tick(kCdromReadCycles);
        assert(irqType(cdrom) == 0x01);
        assert(cdrom.readData() == userByte(3, 0));
        ack(cdrom);
    }

    return 0;
}
