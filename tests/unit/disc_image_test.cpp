#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc_image.h"

#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
std::filesystem::path createRegionTestIso(const std::filesystem::path& root, const char* filename,
                                          const char* bootExe)
{
    using psxrecomp::u8;

    const std::filesystem::path isoPath = root / filename;
    const uint32_t totalSectors = 32;
    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = 2048;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeSector = 22;

    std::vector<u8> image(totalSectors * 2048u, 0);
    const size_t pvdOffset = 16u * 2048u;
    image[pvdOffset] = 1;
    std::memcpy(image.data() + pvdOffset + 1u, "CD001", 5);
    image[pvdOffset + 6u] = 1;

    auto writeLe16 = [&image](size_t offset, uint16_t value)
    {
        image[offset + 0u] = static_cast<u8>(value & 0xFFu);
        image[offset + 1u] = static_cast<u8>((value >> 8) & 0xFFu);
    };
    auto writeLe32 = [&image](size_t offset, uint32_t value)
    {
        image[offset + 0u] = static_cast<u8>(value & 0xFFu);
        image[offset + 1u] = static_cast<u8>((value >> 8) & 0xFFu);
        image[offset + 2u] = static_cast<u8>((value >> 16) & 0xFFu);
        image[offset + 3u] = static_cast<u8>((value >> 24) & 0xFFu);
    };
    auto writeDirRecord =
        [&](size_t offset, const std::string& name, uint32_t lba, uint32_t size, u8 flags)
    {
        const u8 nameLen = static_cast<u8>(name.size());
        const size_t recordLen = 33u + nameLen + (nameLen % 2u == 0u ? 1u : 0u);
        image[offset] = static_cast<u8>(recordLen);
        writeLe32(offset + 2u, lba);
        writeLe32(offset + 10u, size);
        image[offset + 25u] = flags;
        writeLe16(offset + 28u, 1u);
        image[offset + 32u] = nameLen;
        std::memcpy(image.data() + offset + 33u, name.data(), name.size());
        return recordLen;
    };

    writeLe32(pvdOffset + 80u, totalSectors);
    writeLe16(pvdOffset + 120u, 1u);
    writeLe16(pvdOffset + 124u, 1u);
    writeLe16(pvdOffset + 128u, 2048u);

    const size_t rootRecordOffset = pvdOffset + 156u;
    image[rootRecordOffset] = 34u;
    writeLe32(rootRecordOffset + 2u, rootDirSector);
    writeLe32(rootRecordOffset + 10u, rootDirSize);
    image[rootRecordOffset + 25u] = 0x02u;
    writeLe16(rootRecordOffset + 28u, 1u);
    image[rootRecordOffset + 32u] = 1u;
    image[rootRecordOffset + 33u] = 0u;

    const size_t terminatorOffset = 17u * 2048u;
    image[terminatorOffset] = 255u;
    std::memcpy(image.data() + terminatorOffset + 1u, "CD001", 5);
    image[terminatorOffset + 6u] = 1u;

    size_t cursor = rootDirSector * 2048u;
    cursor += writeDirRecord(cursor, std::string("\0", 1), rootDirSector, rootDirSize, 0x02u);
    cursor += writeDirRecord(cursor, std::string("\1", 1), rootDirSector, rootDirSize, 0x02u);
    cursor += writeDirRecord(cursor, "SYSTEM.CNF;1", systemCnfSector, 64u, 0x00u);
    cursor += writeDirRecord(cursor, std::string(bootExe) + ";1", exeSector, 16u, 0x00u);
    (void)cursor;

    const std::string systemCnf = std::string("BOOT = cdrom:\\") + bootExe + ";1\n";
    std::memcpy(image.data() + systemCnfSector * 2048u, systemCnf.data(), systemCnf.size());
    std::memcpy(image.data() + exeSector * 2048u, "PS-X EXE", 8u);

    std::ofstream out(isoPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
    return isoPath;
}

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
    using psxrecomp::runtime::Disc;
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

    const std::filesystem::path jpIso = createRegionTestIso(root, "jp.iso", "SLPS_000.00");
    const std::filesystem::path naIso = createRegionTestIso(root, "na.iso", "SLUS_000.00");
    const std::filesystem::path euIso = createRegionTestIso(root, "eu.iso", "SLES_000.00");

    DiscImage jpImage;
    assert(jpImage.open(jpIso));
    assert(jpImage.region() == Disc::Region::Japan);
    jpImage.close();

    DiscImage naImage;
    assert(naImage.open(naIso));
    assert(naImage.region() == Disc::Region::NorthAmerica);
    naImage.close();

    DiscImage euImage;
    assert(euImage.open(euIso));
    assert(euImage.region() == Disc::Region::Europe);
    euImage.close();

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
    std::filesystem::remove(jpIso);
    std::filesystem::remove(naIso);
    std::filesystem::remove(euIso);
    std::filesystem::remove(binPath);
    std::filesystem::remove(root);
    return 0;
}
