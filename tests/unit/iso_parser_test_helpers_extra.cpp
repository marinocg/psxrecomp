#include "iso_parser_test_helpers_extra.h"

#include "iso_test_helpers.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using iso_test::kSectorSize;

std::filesystem::path createRawIsoFromPlain(const std::filesystem::path& plainIsoPath,
                                            uint32_t rawSectorSize, uint32_t userDataOffset,
                                            uint8_t modeByte)
{
    std::ifstream in(plainIsoPath, std::ios::binary);
    std::vector<uint8_t> plain((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    assert(!plain.empty());
    assert(plain.size() % kSectorSize == 0);
    size_t totalSectors = plain.size() / kSectorSize;
    std::vector<uint8_t> raw(totalSectors * rawSectorSize, 0);

    for (size_t sector = 0; sector < totalSectors; ++sector)
    {
        size_t rawBase = sector * rawSectorSize;
        if (modeByte != 0 && rawSectorSize >= 16)
        {
            raw[rawBase + 15] = modeByte;
        }
        std::memcpy(raw.data() + rawBase + userDataOffset, plain.data() + sector * kSectorSize,
                    kSectorSize);
    }

    auto path = std::filesystem::temp_directory_path() /
                ("psxrecomp_raw_" + std::to_string(rawSectorSize) + "_" +
                 std::to_string(iso_test::generateUniqueSuffix()) + ".bin");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    out.close();
    return path;
}

std::filesystem::path createPlainIsoWithXaExtension()
{
    const uint32_t totalSectors = 32;
    std::vector<uint8_t> image(totalSectors * kSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = kSectorSize;
    const uint32_t xaSector = 21;
    const uint32_t pathTableSector = 18;

    size_t pvdOffset = 16 * kSectorSize;
    image[pvdOffset] = 1;
    std::memcpy(image.data() + pvdOffset + 1, "CD001", 5);
    image[pvdOffset + 6] = 1;
    std::memcpy(image.data() + pvdOffset + 8, "PLAYSTATION", 11);
    std::memcpy(image.data() + pvdOffset + 40, "XA_EXT_TEST", 11);
    iso_test::writeLe32(image, pvdOffset + 80, totalSectors);
    iso_test::writeLe16(image, pvdOffset + 120, 1);
    iso_test::writeLe16(image, pvdOffset + 124, 1);
    iso_test::writeLe16(image, pvdOffset + 128, kSectorSize);

    size_t pathTableOffset = pathTableSector * kSectorSize;
    size_t pathTableCursor = pathTableOffset;
    pathTableCursor = iso_test::writePathTableEntry(image, pathTableCursor, std::string("\0", 1),
                                                    rootDirSector, 1);
    uint32_t pathTableSize = static_cast<uint32_t>(pathTableCursor - pathTableOffset);
    iso_test::writeLe32(image, pvdOffset + 132, pathTableSize);
    iso_test::writeLe32(image, pvdOffset + 140, pathTableSector);

    size_t rootRecordOffset = pvdOffset + 156;
    image[rootRecordOffset] = 34;
    iso_test::writeLe32(image, rootRecordOffset + 2, rootDirSector);
    iso_test::writeLe32(image, rootRecordOffset + 10, rootDirSize);
    image[rootRecordOffset + 25] = 0x02;
    iso_test::writeLe16(image, rootRecordOffset + 28, 1);
    image[rootRecordOffset + 32] = 1;
    image[rootRecordOffset + 33] = 0;

    size_t terminatorOffset = 17 * kSectorSize;
    image[terminatorOffset] = 255;
    std::memcpy(image.data() + terminatorOffset + 1, "CD001", 5);
    image[terminatorOffset + 6] = 1;

    size_t rootDirOffset = rootDirSector * kSectorSize;
    size_t cursor = rootDirOffset;
    cursor += iso_test::writeDirectoryRecord(image, cursor, std::string("\0", 1), rootDirSector,
                                             rootDirSize, 0x02);
    cursor += iso_test::writeDirectoryRecord(image, cursor, std::string("\1", 1), rootDirSector,
                                             rootDirSize, 0x02);
    iso_test::writeDirectoryRecord(image, cursor, "AUDIO.XA;1", xaSector, 512, 0x00);

    std::string xaData = "NOT_RAW_XA_BUT_RESOURCE";
    std::memcpy(image.data() + xaSector * kSectorSize, xaData.data(), xaData.size());

    auto path = std::filesystem::temp_directory_path() /
                ("psxrecomp_xa_ext_" + std::to_string(iso_test::generateUniqueSuffix()) + ".iso");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
    out.close();
    return path;
}

std::filesystem::path createIsoWithBrokenPathTableAndNestedTim()
{
    const uint32_t totalSectors = 48;
    std::vector<uint8_t> image(totalSectors * kSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = kSectorSize;
    const uint32_t dataDirSector = 21;
    const uint32_t timSector = 22;
    const uint32_t pathTableSector = 18;

    size_t pvdOffset = 16 * kSectorSize;
    image[pvdOffset] = 1;
    std::memcpy(image.data() + pvdOffset + 1, "CD001", 5);
    image[pvdOffset + 6] = 1;
    std::memcpy(image.data() + pvdOffset + 8, "PLAYSTATION", 11);
    std::memcpy(image.data() + pvdOffset + 40, "BROKEN_PATH_TABLE", 16);
    iso_test::writeLe32(image, pvdOffset + 80, totalSectors);
    iso_test::writeLe16(image, pvdOffset + 120, 1);
    iso_test::writeLe16(image, pvdOffset + 124, 1);
    iso_test::writeLe16(image, pvdOffset + 128, kSectorSize);

    size_t pathTableOffset = pathTableSector * kSectorSize;
    size_t pathTableCursor = pathTableOffset;
    pathTableCursor = iso_test::writePathTableEntry(image, pathTableCursor, std::string("\0", 1),
                                                    rootDirSector, 1);
    pathTableCursor =
        iso_test::writePathTableEntry(image, pathTableCursor, "DATA", dataDirSector, 99);
    uint32_t pathTableSize = static_cast<uint32_t>(pathTableCursor - pathTableOffset);
    iso_test::writeLe32(image, pvdOffset + 132, pathTableSize);
    iso_test::writeLe32(image, pvdOffset + 140, pathTableSector);

    size_t rootRecordOffset = pvdOffset + 156;
    image[rootRecordOffset] = 34;
    iso_test::writeLe32(image, rootRecordOffset + 2, rootDirSector);
    iso_test::writeLe32(image, rootRecordOffset + 10, rootDirSize);
    image[rootRecordOffset + 25] = 0x02;
    iso_test::writeLe16(image, rootRecordOffset + 28, 1);
    image[rootRecordOffset + 32] = 1;
    image[rootRecordOffset + 33] = 0;

    size_t terminatorOffset = 17 * kSectorSize;
    image[terminatorOffset] = 255;
    std::memcpy(image.data() + terminatorOffset + 1, "CD001", 5);
    image[terminatorOffset + 6] = 1;

    size_t rootDirOffset = rootDirSector * kSectorSize;
    size_t rootCursor = rootDirOffset;
    rootCursor += iso_test::writeDirectoryRecord(image, rootCursor, std::string("\0", 1),
                                                 rootDirSector, rootDirSize, 0x02);
    rootCursor += iso_test::writeDirectoryRecord(image, rootCursor, std::string("\1", 1),
                                                 rootDirSector, rootDirSize, 0x02);
    iso_test::writeDirectoryRecord(image, rootCursor, "DATA", dataDirSector, kSectorSize, 0x02);

    size_t dataDirOffset = dataDirSector * kSectorSize;
    size_t dataCursor = dataDirOffset;
    dataCursor += iso_test::writeDirectoryRecord(image, dataCursor, std::string("\0", 1),
                                                 dataDirSector, kSectorSize, 0x02);
    dataCursor += iso_test::writeDirectoryRecord(image, dataCursor, std::string("\1", 1),
                                                 rootDirSector, rootDirSize, 0x02);
    iso_test::writeDirectoryRecord(image, dataCursor, "NESTED.TIM;1", timSector, 64, 0x00);

    std::string timData = "TIMDATA";
    std::memcpy(image.data() + timSector * kSectorSize, timData.data(), timData.size());

    auto path = std::filesystem::temp_directory_path() /
                ("psxrecomp_broken_path_table_" + std::to_string(iso_test::generateUniqueSuffix()) +
                 ".iso");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
    out.close();
    return path;
}

SplitCueImage createSplitCueWithGlobalVolumeSpace()
{
    constexpr uint32_t dataTrackSectors = 64;
    constexpr uint32_t audioTrackSectors = 128;
    const uint32_t totalDiscSectors = dataTrackSectors + audioTrackSectors;
    std::vector<uint8_t> dataImage(dataTrackSectors * iso_test::kRawSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = kSectorSize;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeSector = 22;

    std::vector<uint8_t> pvd(kSectorSize, 0);
    pvd[0] = 1;
    std::memcpy(pvd.data() + 1, "CD001", 5);
    pvd[6] = 1;
    std::memcpy(pvd.data() + 8, "PLAYSTATION", 11);
    std::memcpy(pvd.data() + 40, "SPLIT_TRACK_DISC", 16);
    iso_test::writeLe32(pvd, 80, totalDiscSectors);
    iso_test::writeLe16(pvd, 120, 1);
    iso_test::writeLe16(pvd, 124, 1);
    iso_test::writeLe16(pvd, 128, kSectorSize);
    iso_test::writeLe32(pvd, 132, 0);
    size_t rootRecordOffset = 156;
    pvd[rootRecordOffset] = 34;
    iso_test::writeLe32(pvd, rootRecordOffset + 2, rootDirSector);
    iso_test::writeLe32(pvd, rootRecordOffset + 10, rootDirSize);
    pvd[rootRecordOffset + 25] = 0x02;
    iso_test::writeLe16(pvd, rootRecordOffset + 28, 1);
    pvd[rootRecordOffset + 32] = 1;
    pvd[rootRecordOffset + 33] = 0;
    iso_test::writeMode2FormSector(dataImage, 16, pvd, false);

    std::vector<uint8_t> terminator(kSectorSize, 0);
    terminator[0] = 255;
    std::memcpy(terminator.data() + 1, "CD001", 5);
    terminator[6] = 1;
    iso_test::writeMode2FormSector(dataImage, 17, terminator, false);

    std::vector<uint8_t> rootDir(kSectorSize, 0);
    size_t cursor = 0;
    cursor += iso_test::writeDirectoryRecord(rootDir, cursor, std::string("\0", 1), rootDirSector,
                                             rootDirSize, 0x02);
    cursor += iso_test::writeDirectoryRecord(rootDir, cursor, std::string("\1", 1), rootDirSector,
                                             rootDirSize, 0x02);
    cursor +=
        iso_test::writeDirectoryRecord(rootDir, cursor, "SYSTEM.CNF;1", systemCnfSector, 40, 0x00);
    iso_test::writeDirectoryRecord(rootDir, cursor, "GAME.EXE;1", exeSector, 16, 0x00);
    iso_test::writeMode2FormSector(dataImage, rootDirSector, rootDir, false);

    const std::string systemCnf = "BOOT = cdrom:\\GAME.EXE;1\n";
    std::vector<uint8_t> systemData(systemCnf.begin(), systemCnf.end());
    iso_test::writeMode2FormSector(dataImage, systemCnfSector, systemData, false);

    const std::string exeData = "PS-X EXE";
    std::vector<uint8_t> exeBytes(exeData.begin(), exeData.end());
    iso_test::writeMode2FormSector(dataImage, exeSector, exeBytes, false);

    std::vector<uint8_t> audioImage(audioTrackSectors * iso_test::kRawSectorSize, 0);

    const auto uniqueSuffix = iso_test::generateUniqueSuffix();
    auto tempDir = std::filesystem::temp_directory_path();
    const auto dataTrackPath =
        tempDir / ("psxrecomp_split_track_data_" + std::to_string(uniqueSuffix) + ".bin");
    const auto audioTrackPath =
        tempDir / ("psxrecomp_split_track_audio_" + std::to_string(uniqueSuffix) + ".bin");
    const auto cuePath =
        tempDir / ("psxrecomp_split_track_" + std::to_string(uniqueSuffix) + ".cue");

    {
        std::ofstream dataOut(dataTrackPath, std::ios::binary);
        dataOut.write(reinterpret_cast<const char*>(dataImage.data()),
                      static_cast<std::streamsize>(dataImage.size()));
    }
    {
        std::ofstream audioOut(audioTrackPath, std::ios::binary);
        audioOut.write(reinterpret_cast<const char*>(audioImage.data()),
                       static_cast<std::streamsize>(audioImage.size()));
    }
    {
        std::ofstream cueOut(cuePath);
        cueOut << "FILE \"" << dataTrackPath.filename().string() << "\" BINARY\n";
        cueOut << "  TRACK 01 MODE2/2352\n";
        cueOut << "    INDEX 01 00:00:00\n";
        cueOut << "FILE \"" << audioTrackPath.filename().string() << "\" BINARY\n";
        cueOut << "  TRACK 02 AUDIO\n";
        cueOut << "    INDEX 00 00:00:00\n";
        cueOut << "    INDEX 01 00:02:00\n";
    }

    SplitCueImage image{};
    image.cuePath = cuePath;
    image.dataTrackPath = dataTrackPath;
    image.audioTrackPath = audioTrackPath;
    image.totalDiscSectors = totalDiscSectors;
    return image;
}
