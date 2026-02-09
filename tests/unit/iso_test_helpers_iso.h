#pragma once

#include "iso_test_helpers_base.h"

namespace iso_test
{

inline std::filesystem::path createTestIso()
{
    const uint32_t totalSectors = 32;
    std::vector<uint8_t> image(totalSectors * kSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = kSectorSize;
    const uint32_t systemCnfSector = 21;
    const uint32_t dataDirSector = 22;
    const uint32_t exeSector = 23;
    const uint32_t pathTableSector = 18;

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
    size_t pathTableOffset = pathTableSector * kSectorSize;
    size_t pathTableCursor = pathTableOffset;
    pathTableCursor =
        writePathTableEntry(image, pathTableCursor, std::string("\0", 1), rootDirSector, 1);
    pathTableCursor = writePathTableEntry(image, pathTableCursor, "DATA", dataDirSector, 1);
    uint32_t pathTableSize = static_cast<uint32_t>(pathTableCursor - pathTableOffset);

    writeLe32(image, pvdOffset + 132, pathTableSize);
    writeLe32(image, pvdOffset + 140, pathTableSector);
    writeLe32(image, pvdOffset + 148, 0);

    size_t rootRecordOffset = pvdOffset + 156;
    image[rootRecordOffset] = 34;
    writeLe32(image, rootRecordOffset + 2, rootDirSector);
    writeLe32(image, rootRecordOffset + 10, rootDirSize);
    image[rootRecordOffset + 25] = 0x02;
    writeLe16(image, rootRecordOffset + 28, 1);
    image[rootRecordOffset + 32] = 1;
    image[rootRecordOffset + 33] = 0;

    size_t terminatorOffset = 17 * kSectorSize;
    image[terminatorOffset] = 255;
    std::memcpy(image.data() + terminatorOffset + 1, "CD001", 5);
    image[terminatorOffset + 6] = 1;

    size_t rootDirOffset = rootDirSector * kSectorSize;
    size_t cursor = rootDirOffset;
    cursor +=
        writeDirectoryRecord(image, cursor, std::string("\0", 1), rootDirSector, rootDirSize, 0x02);
    cursor +=
        writeDirectoryRecord(image, cursor, std::string("\1", 1), rootDirSector, rootDirSize, 0x02);
    cursor += writeDirectoryRecord(image, cursor, "SYSTEM.CNF;1", systemCnfSector, 64, 0x00);
    cursor += writeDirectoryRecord(image, cursor, "DATA", dataDirSector, kSectorSize, 0x02);
    (void)cursor;

    size_t dataDirOffset = dataDirSector * kSectorSize;
    size_t dataCursor = dataDirOffset;
    dataCursor += writeDirectoryRecord(image, dataCursor, std::string("\0", 1), dataDirSector,
                                       kSectorSize, 0x02);
    dataCursor += writeDirectoryRecord(image, dataCursor, std::string("\1", 1), rootDirSector,
                                       rootDirSize, 0x02);
    writeDirectoryRecord(image, dataCursor, "GAME.EXE;1", exeSector, 16, 0x00);

    std::string systemCnf = "Boot = cdrom0:\\data\\game.exe;1\n";
    std::memcpy(image.data() + systemCnfSector * kSectorSize, systemCnf.data(), systemCnf.size());

    std::string exeData = "PS-X EXE";
    std::memcpy(image.data() + exeSector * kSectorSize, exeData.data(), exeData.size());

    auto uniqueSuffix = generateUniqueSuffix();
    auto path = std::filesystem::temp_directory_path() /
                ("psxrecomp_test_" + std::to_string(uniqueSuffix) + ".iso");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
    out.close();
    return path;
}

inline std::filesystem::path createTestIsoWithLabel(const std::string& label)
{
    const uint32_t totalSectors = 32;
    std::vector<uint8_t> image(totalSectors * kSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = kSectorSize;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeSector = 22;

    size_t pvdOffset = 16 * kSectorSize;
    image[pvdOffset] = 1;
    std::memcpy(image.data() + pvdOffset + 1, "CD001", 5);
    image[pvdOffset + 6] = 1;
    std::memcpy(image.data() + pvdOffset + 8, "PLAYSTATION", 11);
    std::memcpy(image.data() + pvdOffset + 40, label.data(),
                std::min(label.size(), static_cast<size_t>(31)));
    writeLe32(image, pvdOffset + 80, totalSectors);
    writeLe16(image, pvdOffset + 120, 1);
    writeLe16(image, pvdOffset + 124, 1);
    writeLe16(image, pvdOffset + 128, kSectorSize);
    size_t rootRecordOffset = pvdOffset + 156;
    image[rootRecordOffset] = 34;
    writeLe32(image, rootRecordOffset + 2, rootDirSector);
    writeLe32(image, rootRecordOffset + 10, rootDirSize);
    image[rootRecordOffset + 25] = 0x02;
    writeLe16(image, rootRecordOffset + 28, 1);
    image[rootRecordOffset + 32] = 1;
    image[rootRecordOffset + 33] = 0;

    size_t terminatorOffset = 17 * kSectorSize;
    image[terminatorOffset] = 255;
    std::memcpy(image.data() + terminatorOffset + 1, "CD001", 5);
    image[terminatorOffset + 6] = 1;

    size_t rootDirOffset = rootDirSector * kSectorSize;
    size_t cursor = rootDirOffset;
    cursor +=
        writeDirectoryRecord(image, cursor, std::string("\0", 1), rootDirSector, rootDirSize, 0x02);
    cursor +=
        writeDirectoryRecord(image, cursor, std::string("\1", 1), rootDirSector, rootDirSize, 0x02);
    cursor += writeDirectoryRecord(image, cursor, "SYSTEM.CNF;1", systemCnfSector, 40, 0x00);
    writeDirectoryRecord(image, cursor, "GAME.EXE;1", exeSector, 16, 0x00);

    std::string systemCnf = "BOOT = cdrom:\\GAME.EXE;1\n";
    std::memcpy(image.data() + systemCnfSector * kSectorSize, systemCnf.data(), systemCnf.size());

    std::string exeData = "PS-X EXE";
    std::memcpy(image.data() + exeSector * kSectorSize, exeData.data(), exeData.size());

    auto path = std::filesystem::temp_directory_path() / ("psxrecomp_test_" + label + ".iso");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
    out.close();
    return path;
}

} // namespace iso_test
