#include "pipeline_candidate_test_helpers.h"

#include "iso_test_helpers_base.h"
#include "psxrecomp/iso/psx_exe_loader.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace pipeline_candidate_test_support
{
namespace
{

std::vector<uint8_t> buildMinimalExe(uint32_t loadSize, uint32_t loadAddress)
{
    std::vector<uint8_t> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    iso_test::writeLe32(buffer, 0x10, loadAddress);
    iso_test::writeLe32(buffer, 0x14, loadAddress);
    iso_test::writeLe32(buffer, 0x18, loadAddress);
    iso_test::writeLe32(buffer, 0x1C, loadSize);
    return buffer;
}

std::vector<uint8_t> buildMinimalTim()
{
    std::vector<uint8_t> buffer(24, 0);
    iso_test::writeLe32(buffer, 0, 0x00000010);
    iso_test::writeLe32(buffer, 4, 0x00000002);
    iso_test::writeLe32(buffer, 8, 16);
    iso_test::writeLe16(buffer, 12, 0);
    iso_test::writeLe16(buffer, 14, 0);
    iso_test::writeLe16(buffer, 16, 2);
    iso_test::writeLe16(buffer, 18, 1);
    buffer[20] = 0x34;
    buffer[21] = 0x12;
    buffer[22] = 0x78;
    buffer[23] = 0x56;
    return buffer;
}

std::string lbaToCueTimecode(uint32_t lba)
{
    const uint32_t absoluteFrames = lba + 150U;
    const uint32_t minutes = absoluteFrames / (60U * 75U);
    const uint32_t seconds = (absoluteFrames / 75U) % 60U;
    const uint32_t frames = absoluteFrames % 75U;

    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(2) << minutes << ":" << std::setw(2) << seconds << ":"
           << std::setw(2) << frames;
    return stream.str();
}

} // namespace

std::filesystem::path createIsoWithExecutables(const std::string& label,
                                               const std::string& systemCnfContents)
{
    const uint32_t totalSectors = 40;
    std::vector<uint8_t> image(totalSectors * iso_test::kSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = iso_test::kSectorSize;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeASector = 22;
    const uint32_t exeBSector = 24;
    const uint32_t pathTableSector = 18;

    size_t pvdOffset = 16 * iso_test::kSectorSize;
    image[pvdOffset] = 1;
    std::memcpy(image.data() + pvdOffset + 1, "CD001", 5);
    image[pvdOffset + 6] = 1;
    std::memcpy(image.data() + pvdOffset + 8, "PLAYSTATION", 11);
    std::memcpy(image.data() + pvdOffset + 40, label.data(),
                std::min(label.size(), static_cast<size_t>(31)));
    iso_test::writeLe32(image, pvdOffset + 80, totalSectors);
    iso_test::writeLe16(image, pvdOffset + 120, 1);
    iso_test::writeLe16(image, pvdOffset + 124, 1);
    iso_test::writeLe16(image, pvdOffset + 128, iso_test::kSectorSize);

    size_t pathTableOffset = pathTableSector * iso_test::kSectorSize;
    size_t pathTableCursor = pathTableOffset;
    pathTableCursor = iso_test::writePathTableEntry(image, pathTableCursor, std::string("\0", 1),
                                                    rootDirSector, 1);
    uint32_t pathTableSize = static_cast<uint32_t>(pathTableCursor - pathTableOffset);
    iso_test::writeLe32(image, pvdOffset + 132, pathTableSize);
    iso_test::writeLe32(image, pvdOffset + 140, pathTableSector);
    iso_test::writeLe32(image, pvdOffset + 148, 0);

    size_t rootRecordOffset = pvdOffset + 156;
    image[rootRecordOffset] = 34;
    iso_test::writeLe32(image, rootRecordOffset + 2, rootDirSector);
    iso_test::writeLe32(image, rootRecordOffset + 10, rootDirSize);
    image[rootRecordOffset + 25] = 0x02;
    iso_test::writeLe16(image, rootRecordOffset + 28, 1);
    image[rootRecordOffset + 32] = 1;
    image[rootRecordOffset + 33] = 0;

    size_t terminatorOffset = 17 * iso_test::kSectorSize;
    image[terminatorOffset] = 255;
    std::memcpy(image.data() + terminatorOffset + 1, "CD001", 5);
    image[terminatorOffset + 6] = 1;

    size_t rootDirOffset = rootDirSector * iso_test::kSectorSize;
    size_t cursor = rootDirOffset;
    cursor += iso_test::writeDirectoryRecord(image, cursor, std::string("\0", 1), rootDirSector,
                                             rootDirSize, 0x02);
    cursor += iso_test::writeDirectoryRecord(image, cursor, std::string("\1", 1), rootDirSector,
                                             rootDirSize, 0x02);

    const auto exeAData = buildMinimalExe(16, 0x80010000);
    const auto exeBData = buildMinimalExe(16, 0x80020000);
    cursor +=
        iso_test::writeDirectoryRecord(image, cursor, "SYSTEM.CNF;1", systemCnfSector, 128, 0x00);
    cursor += iso_test::writeDirectoryRecord(image, cursor, "GAMEA.EXE;1", exeASector,
                                             static_cast<uint32_t>(exeAData.size()), 0x00);
    iso_test::writeDirectoryRecord(image, cursor, "GAMEB.EXE;1", exeBSector,
                                   static_cast<uint32_t>(exeBData.size()), 0x00);

    std::memcpy(image.data() + systemCnfSector * iso_test::kSectorSize, systemCnfContents.data(),
                systemCnfContents.size());

    std::memcpy(image.data() + exeASector * iso_test::kSectorSize, exeAData.data(),
                exeAData.size());
    std::memcpy(image.data() + exeBSector * iso_test::kSectorSize, exeBData.data(),
                exeBData.size());

    auto uniqueSuffix = iso_test::generateUniqueSuffix();
    auto path = std::filesystem::temp_directory_path() /
                ("psxrecomp_candidates_" + std::to_string(uniqueSuffix) + ".iso");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
    out.close();
    return path;
}

std::filesystem::path createIsoWithTimBin(const std::string& label)
{
    const uint32_t totalSectors = 48;
    std::vector<uint8_t> image(totalSectors * iso_test::kSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = iso_test::kSectorSize;
    const uint32_t pathTableSector = 18;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeSector = 22;
    const uint32_t timSector = 24;

    size_t pvdOffset = 16 * iso_test::kSectorSize;
    image[pvdOffset] = 1;
    std::memcpy(image.data() + pvdOffset + 1, "CD001", 5);
    image[pvdOffset + 6] = 1;
    std::memcpy(image.data() + pvdOffset + 8, "PLAYSTATION", 11);
    std::memcpy(image.data() + pvdOffset + 40, label.data(),
                std::min(label.size(), static_cast<size_t>(31)));
    iso_test::writeLe32(image, pvdOffset + 80, totalSectors);
    iso_test::writeLe16(image, pvdOffset + 120, 1);
    iso_test::writeLe16(image, pvdOffset + 124, 1);
    iso_test::writeLe16(image, pvdOffset + 128, iso_test::kSectorSize);

    size_t pathTableOffset = pathTableSector * iso_test::kSectorSize;
    size_t pathTableCursor = pathTableOffset;
    pathTableCursor = iso_test::writePathTableEntry(image, pathTableCursor, std::string("\0", 1),
                                                    rootDirSector, 1);
    uint32_t pathTableSize = static_cast<uint32_t>(pathTableCursor - pathTableOffset);
    iso_test::writeLe32(image, pvdOffset + 132, pathTableSize);
    iso_test::writeLe32(image, pvdOffset + 140, pathTableSector);
    iso_test::writeLe32(image, pvdOffset + 148, 0);

    size_t rootRecordOffset = pvdOffset + 156;
    image[rootRecordOffset] = 34;
    iso_test::writeLe32(image, rootRecordOffset + 2, rootDirSector);
    iso_test::writeLe32(image, rootRecordOffset + 10, rootDirSize);
    image[rootRecordOffset + 25] = 0x02;
    iso_test::writeLe16(image, rootRecordOffset + 28, 1);
    image[rootRecordOffset + 32] = 1;
    image[rootRecordOffset + 33] = 0;

    size_t terminatorOffset = 17 * iso_test::kSectorSize;
    image[terminatorOffset] = 255;
    std::memcpy(image.data() + terminatorOffset + 1, "CD001", 5);
    image[terminatorOffset + 6] = 1;

    const auto exeData = buildMinimalExe(16, 0x80030000);
    const auto timData = buildMinimalTim();
    const std::string systemCnf = "BOOT = cdrom:\\GAME.EXE;1\n";

    size_t rootDirOffset = rootDirSector * iso_test::kSectorSize;
    size_t cursor = rootDirOffset;
    cursor += iso_test::writeDirectoryRecord(image, cursor, std::string("\0", 1), rootDirSector,
                                             rootDirSize, 0x02);
    cursor += iso_test::writeDirectoryRecord(image, cursor, std::string("\1", 1), rootDirSector,
                                             rootDirSize, 0x02);
    cursor +=
        iso_test::writeDirectoryRecord(image, cursor, "SYSTEM.CNF;1", systemCnfSector, 128, 0x00);
    cursor += iso_test::writeDirectoryRecord(image, cursor, "GAME.EXE;1", exeSector,
                                             static_cast<uint32_t>(exeData.size()), 0x00);
    iso_test::writeDirectoryRecord(image, cursor, "TEXTURE.BIN;1", timSector,
                                   static_cast<uint32_t>(timData.size()), 0x00);

    std::memcpy(image.data() + systemCnfSector * iso_test::kSectorSize, systemCnf.data(),
                systemCnf.size());
    std::memcpy(image.data() + exeSector * iso_test::kSectorSize, exeData.data(), exeData.size());
    std::memcpy(image.data() + timSector * iso_test::kSectorSize, timData.data(), timData.size());

    auto uniqueSuffix = iso_test::generateUniqueSuffix();
    auto path = std::filesystem::temp_directory_path() /
                ("psxrecomp_tim_bin_" + std::to_string(uniqueSuffix) + ".iso");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
    out.close();
    return path;
}

std::filesystem::path createIsoWithEmbeddedTimContainer(const std::string& label)
{
    const uint32_t totalSectors = 240;
    std::vector<uint8_t> image(totalSectors * iso_test::kSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = iso_test::kSectorSize;
    const uint32_t pathTableSector = 18;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeSector = 22;
    const uint32_t containerSector = 24;

    size_t pvdOffset = 16 * iso_test::kSectorSize;
    image[pvdOffset] = 1;
    std::memcpy(image.data() + pvdOffset + 1, "CD001", 5);
    image[pvdOffset + 6] = 1;
    std::memcpy(image.data() + pvdOffset + 8, "PLAYSTATION", 11);
    std::memcpy(image.data() + pvdOffset + 40, label.data(),
                std::min(label.size(), static_cast<size_t>(31)));
    iso_test::writeLe32(image, pvdOffset + 80, totalSectors);
    iso_test::writeLe16(image, pvdOffset + 120, 1);
    iso_test::writeLe16(image, pvdOffset + 124, 1);
    iso_test::writeLe16(image, pvdOffset + 128, iso_test::kSectorSize);

    size_t pathTableOffset = pathTableSector * iso_test::kSectorSize;
    size_t pathTableCursor = pathTableOffset;
    pathTableCursor = iso_test::writePathTableEntry(image, pathTableCursor, std::string("\0", 1),
                                                    rootDirSector, 1);
    uint32_t pathTableSize = static_cast<uint32_t>(pathTableCursor - pathTableOffset);
    iso_test::writeLe32(image, pvdOffset + 132, pathTableSize);
    iso_test::writeLe32(image, pvdOffset + 140, pathTableSector);
    iso_test::writeLe32(image, pvdOffset + 148, 0);

    size_t rootRecordOffset = pvdOffset + 156;
    image[rootRecordOffset] = 34;
    iso_test::writeLe32(image, rootRecordOffset + 2, rootDirSector);
    iso_test::writeLe32(image, rootRecordOffset + 10, rootDirSize);
    image[rootRecordOffset + 25] = 0x02;
    iso_test::writeLe16(image, rootRecordOffset + 28, 1);
    image[rootRecordOffset + 32] = 1;
    image[rootRecordOffset + 33] = 0;

    size_t terminatorOffset = 17 * iso_test::kSectorSize;
    image[terminatorOffset] = 255;
    std::memcpy(image.data() + terminatorOffset + 1, "CD001", 5);
    image[terminatorOffset + 6] = 1;

    const auto exeData = buildMinimalExe(16, 0x80040000);
    const auto timData = buildMinimalTim();
    std::vector<uint8_t> containerData(300 * 1024, 0xAB);
    const size_t timOffset = 0x1234;
    std::memcpy(containerData.data() + static_cast<std::ptrdiff_t>(timOffset), timData.data(),
                timData.size());
    const std::string systemCnf = "BOOT = cdrom:\\GAME.EXE;1\n";

    size_t rootDirOffset = rootDirSector * iso_test::kSectorSize;
    size_t cursor = rootDirOffset;
    cursor += iso_test::writeDirectoryRecord(image, cursor, std::string("\0", 1), rootDirSector,
                                             rootDirSize, 0x02);
    cursor += iso_test::writeDirectoryRecord(image, cursor, std::string("\1", 1), rootDirSector,
                                             rootDirSize, 0x02);
    cursor +=
        iso_test::writeDirectoryRecord(image, cursor, "SYSTEM.CNF;1", systemCnfSector, 128, 0x00);
    cursor += iso_test::writeDirectoryRecord(image, cursor, "GAME.EXE;1", exeSector,
                                             static_cast<uint32_t>(exeData.size()), 0x00);
    iso_test::writeDirectoryRecord(image, cursor, "CONTAINER.BIN;1", containerSector,
                                   static_cast<uint32_t>(containerData.size()), 0x00);

    std::memcpy(image.data() + systemCnfSector * iso_test::kSectorSize, systemCnf.data(),
                systemCnf.size());
    std::memcpy(image.data() + exeSector * iso_test::kSectorSize, exeData.data(), exeData.size());
    std::memcpy(image.data() + containerSector * iso_test::kSectorSize, containerData.data(),
                containerData.size());

    auto uniqueSuffix = iso_test::generateUniqueSuffix();
    auto path = std::filesystem::temp_directory_path() /
                ("psxrecomp_embedded_tim_" + std::to_string(uniqueSuffix) + ".iso");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
    out.close();
    return path;
}

std::filesystem::path createIsoForExportPolicy(const std::string& label)
{
    const uint32_t totalSectors = 96;
    std::vector<uint8_t> image(totalSectors * iso_test::kSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = iso_test::kSectorSize;
    const uint32_t pathTableSector = 18;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeASector = 22;
    const uint32_t exeBSector = 24;
    const uint32_t smallDataSector = 26;
    const uint32_t mediumDataSector = 27;
    const uint32_t largeDataSector = 30;

    size_t pvdOffset = 16 * iso_test::kSectorSize;
    image[pvdOffset] = 1;
    std::memcpy(image.data() + pvdOffset + 1, "CD001", 5);
    image[pvdOffset + 6] = 1;
    std::memcpy(image.data() + pvdOffset + 8, "PLAYSTATION", 11);
    std::memcpy(image.data() + pvdOffset + 40, label.data(),
                std::min(label.size(), static_cast<size_t>(31)));
    iso_test::writeLe32(image, pvdOffset + 80, totalSectors);
    iso_test::writeLe16(image, pvdOffset + 120, 1);
    iso_test::writeLe16(image, pvdOffset + 124, 1);
    iso_test::writeLe16(image, pvdOffset + 128, iso_test::kSectorSize);

    size_t pathTableOffset = pathTableSector * iso_test::kSectorSize;
    size_t pathTableCursor = pathTableOffset;
    pathTableCursor = iso_test::writePathTableEntry(image, pathTableCursor, std::string("\0", 1),
                                                    rootDirSector, 1);
    uint32_t pathTableSize = static_cast<uint32_t>(pathTableCursor - pathTableOffset);
    iso_test::writeLe32(image, pvdOffset + 132, pathTableSize);
    iso_test::writeLe32(image, pvdOffset + 140, pathTableSector);
    iso_test::writeLe32(image, pvdOffset + 148, 0);

    size_t rootRecordOffset = pvdOffset + 156;
    image[rootRecordOffset] = 34;
    iso_test::writeLe32(image, rootRecordOffset + 2, rootDirSector);
    iso_test::writeLe32(image, rootRecordOffset + 10, rootDirSize);
    image[rootRecordOffset + 25] = 0x02;
    iso_test::writeLe16(image, rootRecordOffset + 28, 1);
    image[rootRecordOffset + 32] = 1;
    image[rootRecordOffset + 33] = 0;

    size_t terminatorOffset = 17 * iso_test::kSectorSize;
    image[terminatorOffset] = 255;
    std::memcpy(image.data() + terminatorOffset + 1, "CD001", 5);
    image[terminatorOffset + 6] = 1;

    size_t rootDirOffset = rootDirSector * iso_test::kSectorSize;
    size_t cursor = rootDirOffset;
    cursor += iso_test::writeDirectoryRecord(image, cursor, std::string("\0", 1), rootDirSector,
                                             rootDirSize, 0x02);
    cursor += iso_test::writeDirectoryRecord(image, cursor, std::string("\1", 1), rootDirSector,
                                             rootDirSize, 0x02);

    const auto exeAData = buildMinimalExe(16, 0x80010000);
    const auto exeBData = buildMinimalExe(16, 0x80020000);
    const std::string systemCnf = "BOOT = cdrom:\\GAMEB.EXE;1\n";
    const std::vector<uint8_t> smallData(512, 0x11);
    const std::vector<uint8_t> mediumData(1536, 0x22);
    const std::vector<uint8_t> largeData(4096, 0x33);

    cursor +=
        iso_test::writeDirectoryRecord(image, cursor, "SYSTEM.CNF;1", systemCnfSector, 128, 0x00);
    cursor += iso_test::writeDirectoryRecord(image, cursor, "GAMEA.EXE;1", exeASector,
                                             static_cast<uint32_t>(exeAData.size()), 0x00);
    cursor += iso_test::writeDirectoryRecord(image, cursor, "GAMEB.EXE;1", exeBSector,
                                             static_cast<uint32_t>(exeBData.size()), 0x00);
    cursor += iso_test::writeDirectoryRecord(image, cursor, "SMALL.DAT;1", smallDataSector,
                                             static_cast<uint32_t>(smallData.size()), 0x00);
    cursor += iso_test::writeDirectoryRecord(image, cursor, "MEDIUM.DAT;1", mediumDataSector,
                                             static_cast<uint32_t>(mediumData.size()), 0x00);
    iso_test::writeDirectoryRecord(image, cursor, "LARGE.DAT;1", largeDataSector,
                                   static_cast<uint32_t>(largeData.size()), 0x00);

    std::memcpy(image.data() + systemCnfSector * iso_test::kSectorSize, systemCnf.data(),
                systemCnf.size());
    std::memcpy(image.data() + exeASector * iso_test::kSectorSize, exeAData.data(),
                exeAData.size());
    std::memcpy(image.data() + exeBSector * iso_test::kSectorSize, exeBData.data(),
                exeBData.size());
    std::memcpy(image.data() + smallDataSector * iso_test::kSectorSize, smallData.data(),
                smallData.size());
    std::memcpy(image.data() + mediumDataSector * iso_test::kSectorSize, mediumData.data(),
                mediumData.size());
    std::memcpy(image.data() + largeDataSector * iso_test::kSectorSize, largeData.data(),
                largeData.size());

    auto uniqueSuffix = iso_test::generateUniqueSuffix();
    auto path = std::filesystem::temp_directory_path() /
                ("psxrecomp_policy_" + std::to_string(uniqueSuffix) + ".iso");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
    out.close();
    return path;
}

CueImagePaths createCueWithDataTrackOffset(const std::string& label, uint32_t trackStartLba)
{
    const uint32_t volumeSectors = 64;
    const uint32_t totalSectors = trackStartLba + volumeSectors;
    std::vector<uint8_t> image(totalSectors * iso_test::kRawSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = iso_test::kSectorSize;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeSector = 22;

    std::vector<uint8_t> pvd(iso_test::kSectorSize, 0);
    pvd[0] = 1;
    std::memcpy(pvd.data() + 1, "CD001", 5);
    pvd[6] = 1;
    std::memcpy(pvd.data() + 8, "PLAYSTATION", 11);
    std::memcpy(pvd.data() + 40, label.data(), std::min(label.size(), static_cast<size_t>(31)));
    iso_test::writeLe32(pvd, 80, volumeSectors);
    iso_test::writeLe16(pvd, 120, 1);
    iso_test::writeLe16(pvd, 124, 1);
    iso_test::writeLe16(pvd, 128, iso_test::kSectorSize);
    size_t rootRecordOffset = 156;
    pvd[rootRecordOffset] = 34;
    iso_test::writeLe32(pvd, rootRecordOffset + 2, rootDirSector);
    iso_test::writeLe32(pvd, rootRecordOffset + 10, rootDirSize);
    pvd[rootRecordOffset + 25] = 0x02;
    iso_test::writeLe16(pvd, rootRecordOffset + 28, 1);
    pvd[rootRecordOffset + 32] = 1;
    pvd[rootRecordOffset + 33] = 0;
    iso_test::writeMode2FormSector(image, trackStartLba + 16, pvd, false);

    std::vector<uint8_t> terminator(iso_test::kSectorSize, 0);
    terminator[0] = 255;
    std::memcpy(terminator.data() + 1, "CD001", 5);
    terminator[6] = 1;
    iso_test::writeMode2FormSector(image, trackStartLba + 17, terminator, false);

    std::vector<uint8_t> rootDir(iso_test::kSectorSize, 0);
    size_t cursor = 0;
    cursor += iso_test::writeDirectoryRecord(rootDir, cursor, std::string("\0", 1), rootDirSector,
                                             rootDirSize, 0x02);
    cursor += iso_test::writeDirectoryRecord(rootDir, cursor, std::string("\1", 1), rootDirSector,
                                             rootDirSize, 0x02);
    cursor +=
        iso_test::writeDirectoryRecord(rootDir, cursor, "SYSTEM.CNF;1", systemCnfSector, 64, 0x00);
    const auto exeData = buildMinimalExe(16, 0x80050000);
    iso_test::writeDirectoryRecord(rootDir, cursor, "GAME.EXE;1", exeSector,
                                   static_cast<uint32_t>(exeData.size()), 0x00);
    iso_test::writeMode2FormSector(image, trackStartLba + rootDirSector, rootDir, false);

    const std::string systemCnf = "BOOT = cdrom:\\GAME.EXE;1\n";
    std::vector<uint8_t> systemData(systemCnf.begin(), systemCnf.end());
    iso_test::writeMode2FormSector(image, trackStartLba + systemCnfSector, systemData, false);

    std::vector<uint8_t> exeSector0(iso_test::kSectorSize, 0);
    std::vector<uint8_t> exeSector1(iso_test::kSectorSize, 0);
    std::memcpy(exeSector0.data(), exeData.data(), iso_test::kSectorSize);
    std::memcpy(exeSector1.data(), exeData.data() + iso_test::kSectorSize,
                exeData.size() - iso_test::kSectorSize);
    iso_test::writeMode2FormSector(image, trackStartLba + exeSector, exeSector0, false);
    iso_test::writeMode2FormSector(image, trackStartLba + exeSector + 1, exeSector1, false);

    const auto uniqueSuffix = iso_test::generateUniqueSuffix();
    const auto binPath = std::filesystem::temp_directory_path() /
                         ("psxrecomp_offset_track_" + std::to_string(uniqueSuffix) + ".bin");
    const auto cuePath = std::filesystem::temp_directory_path() /
                         ("psxrecomp_offset_track_" + std::to_string(uniqueSuffix) + ".cue");

    {
        std::ofstream binOut(binPath, std::ios::binary);
        binOut.write(reinterpret_cast<const char*>(image.data()),
                     static_cast<std::streamsize>(image.size()));
    }
    {
        std::ofstream cueOut(cuePath);
        cueOut << "FILE \"" << binPath.filename().string() << "\" BINARY\n";
        cueOut << "  TRACK 01 MODE2/2352\n";
        cueOut << "    INDEX 01 " << lbaToCueTimecode(trackStartLba) << "\n";
    }

    return CueImagePaths{cuePath, binPath};
}

} // namespace pipeline_candidate_test_support
