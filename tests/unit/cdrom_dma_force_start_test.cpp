#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <vector>

namespace
{
constexpr psxrecomp::u32 kReadCycles = 451584u;

psxrecomp::u8 irqType(const psxrecomp::runtime::Cdrom& cdrom)
{
    return static_cast<psxrecomp::u8>(cdrom.readInterruptFlags() & 0x07u);
}

void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    while ((cdrom.readStatus() & (1u << 5u)) != 0u)
    {
        (void)cdrom.readResponse();
    }
    cdrom.writeInterruptFlags(0x07u);
}

std::vector<psxrecomp::u8> makeSector(psxrecomp::u8 base)
{
    std::vector<psxrecomp::u8> sector(2048u, 0u);
    for (size_t i = 0; i < sector.size(); ++i)
    {
        sector[i] = static_cast<psxrecomp::u8>(base + static_cast<psxrecomp::u8>(i & 0xFFu));
    }
    return sector;
}

psxrecomp::u32 wordAt(const std::vector<psxrecomp::u8>& data, size_t offset)
{
    return static_cast<psxrecomp::u32>(data[offset + 0]) |
           (static_cast<psxrecomp::u32>(data[offset + 1]) << 8u) |
           (static_cast<psxrecomp::u32>(data[offset + 2]) << 16u) |
           (static_cast<psxrecomp::u32>(data[offset + 3]) << 24u);
}

void issueReadN(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeInterruptEnable(0x1Fu);
    cdrom.writeCommand(0x06u);
    assert(irqType(cdrom) == 0x03u);
    ack(cdrom);
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01u);
}

psxrecomp::Address cdromDmaBase()
{
    return psxrecomp::runtime::DmaController::ChannelBase +
           psxrecomp::runtime::DmaController::ChannelStride *
               static_cast<psxrecomp::Address>(psxrecomp::runtime::DmaPort::Cdrom);
}
} // namespace

int main()
{
    using psxrecomp::runtime::PsxSystem;

    {
        PsxSystem system;
        assert(system.initialize());

        const auto sector = makeSector(0x10u);
        system.cdrom().enqueueDataSector(sector);
        issueReadN(system.cdrom());

        constexpr psxrecomp::Address dest = 0x00015000u;
        system.write<psxrecomp::u32>(dest, 0xDEADBEEFu);
        system.write<psxrecomp::u32>(cdromDmaBase() + 0x0, dest);
        system.write<psxrecomp::u32>(cdromDmaBase() + 0x4, 0x00000001u);
        system.write<psxrecomp::u32>(cdromDmaBase() + 0x8, 0x01000000u);

        assert((system.cdrom().readStatus() & 0x40u) == 0u);
        assert(system.read<psxrecomp::u32>(dest) == 0xDEADBEEFu);
    }

    {
        PsxSystem system;
        assert(system.initialize());

        const auto sector = makeSector(0x20u);
        system.cdrom().enqueueDataSector(sector);
        issueReadN(system.cdrom());

        constexpr psxrecomp::Address dest = 0x00015020u;
        system.write<psxrecomp::u32>(dest, 0xDEADBEEFu);
        system.write<psxrecomp::u32>(cdromDmaBase() + 0x0, dest);
        system.write<psxrecomp::u32>(cdromDmaBase() + 0x4, 0x00000001u);
        system.write<psxrecomp::u32>(cdromDmaBase() + 0x8, 0x11000000u);

        assert(system.read<psxrecomp::u32>(dest) == wordAt(sector, 0u));
        assert((system.cdrom().readStatus() & 0x40u) == 0u);
        assert(system.cdrom().readData() == 0u);
    }

    return 0;
}
