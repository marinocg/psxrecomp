#include "psxrecomp/iso/iso_parser.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

constexpr uint32_t kSectorSize = 2048;

void writeLe16(std::vector<uint8_t>& buffer, size_t offset, uint16_t value)
{
    buffer[offset] = static_cast<uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void writeLe32(std::vector<uint8_t>& buffer, size_t offset, uint32_t value)
{
    buffer[offset] = static_cast<uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

size_t writeDirectoryRecord(std::vector<uint8_t>& buffer, size_t offset, const std::string& name,
                            uint32_t extent, uint32_t size, uint8_t flags)
{
    uint8_t nameLength = static_cast<uint8_t>(name.size());
    uint8_t recordLength = static_cast<uint8_t>(33 + nameLength + (nameLength % 2 == 0 ? 1 : 0));
    buffer[offset] = recordLength;
    buffer[offset + 1] = 0;
    writeLe32(buffer, offset + 2, extent);
    writeLe32(buffer, offset + 10, size);
    buffer[offset + 25] = flags;
    buffer[offset + 26] = 0;
    buffer[offset + 27] = 0;
    writeLe16(buffer, offset + 28, 1);
    buffer[offset + 32] = nameLength;
    std::memcpy(buffer.data() + offset + 33, name.data(), nameLength);
    return recordLength;
}

std::filesystem::path createTestIso()
{
    const uint32_t totalSectors = 24;
    std::vector<uint8_t> image(totalSectors * kSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = kSectorSize;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeSector = 22;

    // Primary Volume Descriptor at sector 16.
    size_t pvdOffset = 16 * kSectorSize;
    image[pvdOffset] = 1;
    std::memcpy(image.data() + pvdOffset + 1, "CD001", 5);
    image[pvdOffset + 6] = 1;
    std::memcpy(image.data() + pvdOffset + 8, "PLAYSTATION", 11);
    std::memcpy(image.data() + pvdOffset + 40, "PSXRECOMP_TEST", 14);
    writeLe32(image, pvdOffset + 80, totalSectors);
    writeLe16(image, pvdOffset + 120, 1);
    writeLe16(image, pvdOffset + 124, 1);
    writeLe16(image, pvdOffset + 128, kSectorSize);
    writeLe32(image, pvdOffset + 132, 0);

    // Root directory record in PVD.
    size_t rootRecordOffset = pvdOffset + 156;
    image[rootRecordOffset] = 34;
    writeLe32(image, rootRecordOffset + 2, rootDirSector);
    writeLe32(image, rootRecordOffset + 10, rootDirSize);
    image[rootRecordOffset + 25] = 0x02;
    writeLe16(image, rootRecordOffset + 28, 1);
    image[rootRecordOffset + 32] = 1;
    image[rootRecordOffset + 33] = 0;

    // Volume Descriptor Set Terminator at sector 17.
    size_t terminatorOffset = 17 * kSectorSize;
    image[terminatorOffset] = 255;
    std::memcpy(image.data() + terminatorOffset + 1, "CD001", 5);
    image[terminatorOffset + 6] = 1;

    // Root directory entries.
    size_t rootDirOffset = rootDirSector * kSectorSize;
    size_t cursor = rootDirOffset;
    cursor +=
        writeDirectoryRecord(image, cursor, std::string("\0", 1), rootDirSector, rootDirSize, 0x02);
    cursor +=
        writeDirectoryRecord(image, cursor, std::string("\1", 1), rootDirSector, rootDirSize, 0x02);
    cursor += writeDirectoryRecord(image, cursor, "SYSTEM.CNF;1", systemCnfSector, 40, 0x00);
    cursor += writeDirectoryRecord(image, cursor, "GAME.EXE;1", exeSector, 16, 0x00);
    (void)cursor;

    // SYSTEM.CNF contents.
    std::string systemCnf = "BOOT = cdrom:\\GAME.EXE;1\n";
    std::memcpy(image.data() + systemCnfSector * kSectorSize, systemCnf.data(), systemCnf.size());

    // Dummy executable data.
    std::string exeData = "PS-X EXE";
    std::memcpy(image.data() + exeSector * kSectorSize, exeData.data(), exeData.size());

    auto path = std::filesystem::temp_directory_path() / "psxrecomp_test.iso";
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
    out.close();
    return path;
}

} // namespace

int main()
{
    auto isoPath = createTestIso();

    psxrecomp::iso::IsoParser parser(isoPath.string());
    assert(parser.open());
    assert(parser.isValid());
    assert(parser.getVolumeLabel() == "PSXRECOMP_TEST");

    auto systemCnf = parser.extractFile("SYSTEM.CNF");
    assert(!systemCnf.empty());

    auto exeName = parser.findExecutable();
    assert(exeName == "GAME.EXE");

    std::filesystem::remove(isoPath);
    return 0;
}
