#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc_image.h"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    // Drain all response bytes before acknowledging, so none are left in the ack buffer.
    while ((cdrom.readStatus() & (1u << 5)) != 0u)
    {
        (void)cdrom.readResponse();
    }
    cdrom.writeInterruptFlags(0x07u);
}

void enableBufferRead(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0, 0u);
    cdrom.writeReg(3, 0x80u);
}
} // namespace

int main()
{
    using psxrecomp::u8;
    using psxrecomp::runtime::Cdrom;
    using psxrecomp::runtime::DiscImage;

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "psxrecomp_disc_image_test";
    std::filesystem::create_directories(root);

    const std::filesystem::path isoPath = root / "tiny.iso";
    {
        std::ofstream out(isoPath, std::ios::binary);
        std::vector<u8> sector0(2048, 0x11);
        std::vector<u8> sector1(2048, 0x22);
        std::vector<u8> sector2(2048, 0x33);
        out.write(reinterpret_cast<const char*>(sector0.data()),
                  static_cast<std::streamsize>(sector0.size()));
        out.write(reinterpret_cast<const char*>(sector1.data()),
                  static_cast<std::streamsize>(sector1.size()));
        out.write(reinterpret_cast<const char*>(sector2.data()),
                  static_cast<std::streamsize>(sector2.size()));
    }

    DiscImage image;
    assert(image.open(isoPath));
    assert(image.isOpen());
    assert(image.sectorCount() == 3u);

    [[maybe_unused]] std::array<u8, 2048> user{};
    assert(image.readUserSector(0u, std::span<u8, 2048>(user)));
    assert(user[0] == 0x11u && user[2047] == 0x11u);
    assert(image.readUserSector(2u, std::span<u8, 2048>(user)));
    assert(user[0] == 0x33u && user[2047] == 0x33u);
    assert(!image.readUserSector(3u, std::span<u8, 2048>(user))); // out of range
    [[maybe_unused]] std::array<u8, 2352> synthesizedRaw{};
    assert(image.readRawSector2352(1u, std::span<u8, 2352>(synthesizedRaw)));
    assert(synthesizedRaw[0] == 0x00u);
    assert(synthesizedRaw[1] == 0xFFu);
    assert(synthesizedRaw[10] == 0xFFu);
    assert(synthesizedRaw[11] == 0x00u);
    assert(synthesizedRaw[12] == 0x00u); // minute BCD for LBA 1 -> 00:02:01
    assert(synthesizedRaw[13] == 0x02u);
    assert(synthesizedRaw[14] == 0x01u);
    assert(synthesizedRaw[15] == 0x02u); // Mode2
    assert(synthesizedRaw[16] == 0x00u);
    assert(synthesizedRaw[17] == 0x00u);
    assert(synthesizedRaw[18] == 0x08u); // data submode
    assert(synthesizedRaw[19] == 0x00u);
    assert(synthesizedRaw[20] == 0x00u);
    assert(synthesizedRaw[21] == 0x00u);
    assert(synthesizedRaw[22] == 0x08u);
    assert(synthesizedRaw[23] == 0x00u);
    assert(synthesizedRaw[24] == 0x22u && synthesizedRaw[24 + 2047] == 0x22u);

    // CD-ROM transport should be able to source sector bytes from mounted disc backend.
    Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&image);
    cdrom.writeInterruptEnable(0x07);
    cdrom.writeParam(0x00);   // minute (BCD)
    cdrom.writeParam(0x02);   // second (BCD)
    cdrom.writeParam(0x00);   // frame  (BCD)
    cdrom.writeCommand(0x02); // Setloc
    ack(cdrom);
    cdrom.writeCommand(0x06); // ReadN
    ack(cdrom);
    cdrom.tick(451584);
    enableBufferRead(cdrom);
    assert(cdrom.readDma() == 0x11111111u);

    cdrom.writeParam(0x20);   // whole-sector mode
    cdrom.writeCommand(0x0E); // Setmode
    ack(cdrom);
    cdrom.writeParam(0x00);   // minute (BCD)
    cdrom.writeParam(0x02);   // second (BCD)
    cdrom.writeParam(0x01);   // frame  (BCD)
    cdrom.writeCommand(0x02); // Setloc
    ack(cdrom);
    cdrom.writeCommand(0x06); // ReadN
    ack(cdrom);
    cdrom.tick(451584);
    ack(cdrom); // ack ReadN's INT3 so INT1 can publish and arm the FIFO
    enableBufferRead(cdrom);
    assert(cdrom.readDma() == 0x02010200u); // MM:SS:FF:Mode as little-endian word
    assert(cdrom.readDma() == 0x00080000u); // subheader
    assert(cdrom.readDma() == 0x00080000u); // subheader copy

    image.close();

    const std::filesystem::path binPath = root / "tiny.bin";
    {
        std::ofstream out(binPath, std::ios::binary);
        std::vector<u8> raw(2352 * 2, 0x00);
        for (size_t i = 0; i < 2048; ++i)
        {
            raw[24 + i] = 0x5Au;
            raw[2352 + 24 + i] = 0xA5u;
        }
        out.write(reinterpret_cast<const char*>(raw.data()),
                  static_cast<std::streamsize>(raw.size()));
    }

    DiscImage rawImage;
    assert(rawImage.open(binPath));
    assert(rawImage.sectorCount() == 2u);
    assert(rawImage.readUserSector(1u, std::span<u8, 2048>(user)));
    assert(user[0] == 0xA5u && user[2047] == 0xA5u);
    [[maybe_unused]] std::array<u8, 2352> rawSector{};
    assert(rawImage.readRawSector2352(0u, std::span<u8, 2352>(rawSector)));
    assert(rawSector[24] == 0x5Au);
    assert(!rawImage.readRawSector2352(2u, std::span<u8, 2352>(rawSector))); // out of range
    rawImage.close();

    std::filesystem::remove(isoPath);
    std::filesystem::remove(binPath);
    std::filesystem::remove(root);
    return 0;
}
