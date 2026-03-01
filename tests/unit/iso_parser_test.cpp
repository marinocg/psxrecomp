#include "psxrecomp/iso/iso_parser.h"
#include "psxrecomp/iso/multi_disc_set.h"

#include "iso_test_helpers.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using iso_test::createCueImage;
using iso_test::createMultiSessionCue;
using iso_test::createTestIso;
using iso_test::createTestIsoWithLabel;
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

struct SplitCueImage
{
    std::filesystem::path cuePath;
    std::filesystem::path dataTrackPath;
    std::filesystem::path audioTrackPath;
    uint32_t totalDiscSectors = 0;
};

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
    assert(exeName == "DATA/GAME.EXE");

    auto executableList = parser.listExecutables();
    assert(std::find(executableList.begin(), executableList.end(), exeName) !=
           executableList.end());
    auto flatTree = parser.listAllFilesRecursive();
    auto dataDirEntry =
        std::find_if(flatTree.begin(), flatTree.end(), [](const psxrecomp::iso::IsoFileEntry& entry)
                     { return entry.path == "DATA" && entry.isDirectory; });
    assert(dataDirEntry != flatTree.end());
    auto gameExeEntry =
        std::find_if(flatTree.begin(), flatTree.end(), [](const psxrecomp::iso::IsoFileEntry& entry)
                     { return entry.path == "DATA/GAME.EXE" && !entry.isDirectory; });
    assert(gameExeEntry != flatTree.end());
    assert(gameExeEntry->size == 16);
    assert(gameExeEntry->extents.size() == 1);

    auto tracks = parser.getTracks();
    assert(tracks.size() == 1);
    assert(tracks.front().type == psxrecomp::iso::TrackType::Data);
    assert(tracks.front().startLba == 0);

    std::filesystem::remove(isoPath);

    std::filesystem::path cuePath;
    auto binPath = createCueImage(cuePath);
    psxrecomp::iso::IsoParser cueParser(cuePath.string());
    assert(cueParser.open());
    assert(cueParser.isValid());
    assert(cueParser.findExecutable() == "GAME.EXE");

    auto cueTracks = cueParser.getTracks();
    assert(cueTracks.size() == 1);
    assert(cueTracks.front().type == psxrecomp::iso::TrackType::Data);
    assert(cueTracks.front().startLba == 0);
    assert(cueTracks.front().file == binPath.string());

    auto multiExtent = cueParser.extractFile("MULTI.BIN");
    assert(multiExtent.size() == kSectorSize * 2);
    assert(multiExtent.front() == 'A');
    assert(multiExtent.back() == 'B');

    auto xaData = cueParser.extractFile("AUDIO.XA");
    assert(xaData.size() == 2324);
    assert(xaData.front() == 'X');

    auto jolietData = cueParser.extractFile("LONGNAME.TXT");
    assert(!jolietData.empty());

    auto timResources = cueParser.listResources(psxrecomp::iso::ResourceType::TimTexture);
    assert(timResources.size() == 1);
    assert(timResources.front() == "TEXTURE.TIM");

    auto strResources = cueParser.listResources(psxrecomp::iso::ResourceType::StrVideo);
    assert(strResources.size() == 1);
    assert(strResources.front() == "MOVIE.STR");

    auto xaResources = cueParser.listResources(psxrecomp::iso::ResourceType::XaAudio);
    assert(xaResources.size() == 1);
    assert(xaResources.front() == "AUDIO.XA");
    auto cueTree = cueParser.listAllFilesRecursive();
    auto multiEntry =
        std::find_if(cueTree.begin(), cueTree.end(), [](const psxrecomp::iso::IsoFileEntry& entry)
                     { return entry.path == "MULTI.BIN" && !entry.isDirectory; });
    assert(multiEntry != cueTree.end());
    assert(multiEntry->size == kSectorSize * 2);
    assert(multiEntry->extents.size() == 2);
    assert(multiEntry->extents.front().continues);
    assert(!multiEntry->extents.back().continues);

    auto plainXaIso = createPlainIsoWithXaExtension();
    psxrecomp::iso::IsoParser plainXaParser(plainXaIso.string());
    assert(plainXaParser.open());
    assert(plainXaParser.isValid());
    auto plainXaResources = plainXaParser.listResources(psxrecomp::iso::ResourceType::XaAudio);
    assert(plainXaResources.size() == 1);
    assert(plainXaResources.front() == "AUDIO.XA");
    std::filesystem::remove(plainXaIso);

    auto brokenPathTableIso = createIsoWithBrokenPathTableAndNestedTim();
    psxrecomp::iso::IsoParser brokenPathTableParser(brokenPathTableIso.string());
    assert(brokenPathTableParser.open());
    assert(brokenPathTableParser.isValid());
    auto brokenTimResources =
        brokenPathTableParser.listResources(psxrecomp::iso::ResourceType::TimTexture);
    assert(brokenTimResources.size() == 1);
    assert(brokenTimResources.front() == "DATA/NESTED.TIM");
    auto brokenTree = brokenPathTableParser.listAllFilesRecursive();
    auto nestedTimEntry = std::find_if(
        brokenTree.begin(), brokenTree.end(), [](const psxrecomp::iso::IsoFileEntry& entry)
        { return entry.path == "DATA/NESTED.TIM" && !entry.isDirectory; });
    assert(nestedTimEntry != brokenTree.end());
    bool hasPathTableError = false;
    for (const auto& error : brokenPathTableParser.getErrors())
    {
        if (error.find("path table") != std::string::npos ||
            error.find("Path table") != std::string::npos)
        {
            hasPathTableError = true;
            break;
        }
    }
    assert(hasPathTableError);
    std::filesystem::remove(brokenPathTableIso);

    auto plainForRaw2352Mode1 = createTestIso();
    auto raw2352Mode1 = createRawIsoFromPlain(plainForRaw2352Mode1, 2352, 16, 1);
    psxrecomp::iso::IsoParser rawMode1Parser(raw2352Mode1.string());
    assert(rawMode1Parser.open());
    assert(rawMode1Parser.isValid());
    assert(rawMode1Parser.findExecutable() == "DATA/GAME.EXE");
    std::filesystem::remove(raw2352Mode1);
    std::filesystem::remove(plainForRaw2352Mode1);

    auto plainForRaw2352Mode2 = createTestIso();
    auto raw2352Mode2 = createRawIsoFromPlain(plainForRaw2352Mode2, 2352, 24, 2);
    psxrecomp::iso::IsoParser rawMode2Parser(raw2352Mode2.string());
    assert(rawMode2Parser.open());
    assert(rawMode2Parser.isValid());
    assert(rawMode2Parser.findExecutable() == "DATA/GAME.EXE");
    std::filesystem::remove(raw2352Mode2);
    std::filesystem::remove(plainForRaw2352Mode2);

    auto plainForRaw2336 = createTestIso();
    auto raw2336 = createRawIsoFromPlain(plainForRaw2336, 2336, 0, 0);
    psxrecomp::iso::IsoParser raw2336Parser(raw2336.string());
    assert(raw2336Parser.open());
    assert(raw2336Parser.isValid());
    assert(raw2336Parser.findExecutable() == "DATA/GAME.EXE");
    std::filesystem::remove(raw2336);
    std::filesystem::remove(plainForRaw2336);

    auto exportDir = std::filesystem::temp_directory_path() /
                     ("psxrecomp_exports_" + std::to_string(iso_test::generateUniqueSuffix()));
    assert(cueParser.exportResources(psxrecomp::iso::ResourceType::TimTexture, exportDir.string()));
    assert(std::filesystem::exists(exportDir / "TEXTURE.TIM"));

    std::error_code cleanupError;
    std::filesystem::remove_all(exportDir, cleanupError);

    std::filesystem::remove(cuePath);
    std::filesystem::remove(binPath);

    auto splitCue = createSplitCueWithGlobalVolumeSpace();
    psxrecomp::iso::IsoParser splitCueParser(splitCue.cuePath.string());
    assert(splitCueParser.open());
    assert(splitCueParser.isValid());
    assert(splitCueParser.findExecutable() == "GAME.EXE");
    assert(splitCueParser.getTotalSectors() == splitCue.totalDiscSectors);
    bool hasVolumeSizeError = false;
    for (const auto& error : splitCueParser.getErrors())
    {
        if (error.find("Volume space size exceeds image size.") != std::string::npos)
        {
            hasVolumeSizeError = true;
            break;
        }
    }
    assert(!hasVolumeSizeError);
    std::filesystem::remove(splitCue.cuePath);
    std::filesystem::remove(splitCue.dataTrackPath);
    std::filesystem::remove(splitCue.audioTrackPath);

    std::filesystem::path multiCuePath;
    std::filesystem::path sessionTwoBin;
    auto sessionOneBin = createMultiSessionCue(multiCuePath, sessionTwoBin);
    psxrecomp::iso::IsoParser multiParser(multiCuePath.string());
    assert(multiParser.open());
    assert(multiParser.isValid());
    assert(multiParser.getVolumeLabel() == "SESSION_TWO");
    auto multiTracks = multiParser.getTracks();
    assert(multiTracks.size() == 2);
    auto primaryTrack = multiParser.getDataTrack();
    assert(primaryTrack.has_value());
    assert(primaryTrack->sessionNumber == 2);
    assert(multiTracks[1].pregapLength == 150);

    std::filesystem::remove(multiCuePath);
    std::filesystem::remove(sessionOneBin);
    std::filesystem::remove(sessionTwoBin);

    auto isoA = createTestIsoWithLabel("DISC_ONE");
    auto isoB = createTestIsoWithLabel("DISC_TWO");
    psxrecomp::iso::MultiDiscSet discSet({isoA.string(), isoB.string()});
    assert(discSet.open());
    assert(discSet.getDiscCount() == 2);
    assert(discSet.getDiscInfo(1).volumeLabel == "DISC_TWO");
    auto cumulativeTracks = discSet.getCumulativeTracks();
    assert(!cumulativeTracks.empty());
    assert(cumulativeTracks.front().discIndex == 0);
    assert(discSet.setActiveDisc(1));
    assert(discSet.getActiveDiscIndex() == 1);

    std::filesystem::remove(isoA);
    std::filesystem::remove(isoB);
    return 0;
}
