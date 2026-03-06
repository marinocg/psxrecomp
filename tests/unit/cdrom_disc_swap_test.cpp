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
};

class FailingDisc final : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(psxrecomp::u32, std::span<psxrecomp::u8, 2048>) override
    {
        return false;
    }
};

psxrecomp::u8 irqType(const psxrecomp::runtime::Cdrom& cdrom)
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

void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeInterruptFlags(0x07);
}
} // namespace

int main()
{
    using psxrecomp::runtime::Cdrom;

    // Disc swap should expose shell-open status, then close after transition delay.
    {
        Cdrom cdrom;
        PatternDisc disc1;
        PatternDisc disc2;
        cdrom.reset();
        cdrom.setDiscBackend(&disc1);
        cdrom.writeInterruptEnable(0x1F);

        cdrom.writeCommand(0x01); // Getstat baseline
        assert(irqType(cdrom) == 0x03);
        const auto baseline = readResponse(cdrom, 1);
        assert((baseline[0] & 0x10u) == 0u);
        ack(cdrom);

        cdrom.setDiscBackend(&disc2);
        cdrom.writeCommand(0x01); // Getstat during door-open transition
        assert(irqType(cdrom) == 0x03);
        const auto doorOpen = readResponse(cdrom, 1);
        assert((doorOpen[0] & 0x10u) != 0u);
        ack(cdrom);

        cdrom.tick(kCdromReadCycles * 2u);
        cdrom.writeCommand(0x01); // Getstat after transition closes
        assert(irqType(cdrom) == 0x03);
        const auto doorClosed = readResponse(cdrom, 1);
        assert((doorClosed[0] & 0x10u) == 0u);
        ack(cdrom);
    }

    // Read command with no disc should return INT5 no-disc error.
    {
        Cdrom cdrom;
        cdrom.reset();
        cdrom.setDiscBackend(nullptr);
        cdrom.writeInterruptEnable(0x1F);

        cdrom.writeCommand(0x06); // ReadN
        assert(irqType(cdrom) == 0x05);
        const auto error = readResponse(cdrom, 8);
        assert((error[0] & 0x08u) != 0u); // ID error bit
        assert(error[1] == 0x40u);        // no disc
        ack(cdrom);
    }

    // Disc read failure should return INT5 read-fail error code.
    {
        Cdrom cdrom;
        FailingDisc disc;
        cdrom.reset();
        cdrom.setDiscBackend(&disc);
        cdrom.writeInterruptEnable(0x1F);

        cdrom.writeParam(0x00);
        cdrom.writeParam(0x02);
        cdrom.writeParam(0x00);
        cdrom.writeCommand(0x02); // Setloc
        assert(irqType(cdrom) == 0x03);
        (void)cdrom.readResponse();
        ack(cdrom);

        cdrom.writeCommand(0x06); // ReadN start
        assert(irqType(cdrom) == 0x03);
        (void)cdrom.readResponse();
        ack(cdrom);

        cdrom.tick(kCdromReadCycles);
        assert(irqType(cdrom) == 0x05);
        const auto error = readResponse(cdrom, 8);
        assert((error[0] & 0x08u) != 0u); // ID error bit
        assert(error[1] == 0x80u);        // read fail
        ack(cdrom);
    }

    return 0;
}
