#pragma once

#include "iso_test_helpers_base.h"

namespace iso_test
{

inline std::filesystem::path createCueImage(std::filesystem::path& cuePath)
{
    const uint32_t dataTrackStart = 0;
    const uint32_t totalSectors = 200;
    std::vector<uint8_t> image(totalSectors * kRawSectorSize, 0);

    auto uniqueSuffix = generateUniqueSuffix();
    auto uniqueName = std::string("psxrecomp_test_") + std::to_string(uniqueSuffix) + ".";
    auto binPath = std::filesystem::temp_directory_path() / (uniqueName + "bin");
    cuePath = std::filesystem::temp_directory_path() / (uniqueName + "cue");

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = kSectorSize;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeSector = 22;
    const uint32_t multiExtentSectorA = 23;
    const uint32_t multiExtentSectorB = 24;
    const uint32_t xaSector = 25;
    const uint32_t timSector = 26;
    const uint32_t strSector = 27;
    const uint32_t jolietRootSector = 28;

    std::vector<uint8_t> pvd(kSectorSize, 0);
    pvd[0] = 1;
    std::memcpy(pvd.data() + 1, "CD001", 5);
    pvd[6] = 1;
    std::memcpy(pvd.data() + 8, "PLAYSTATION", 11);
    std::memcpy(pvd.data() + 40, "PSXRECOMP_CUE", 13);
    writeLe32(pvd, 80, totalSectors);
    writeLe16(pvd, 120, 1);
    writeLe16(pvd, 124, 1);
    writeLe16(pvd, 128, kSectorSize);
    writeLe32(pvd, 132, 0);
    size_t rootRecordOffset = 156;
    pvd[rootRecordOffset] = 34;
    writeLe32(pvd, rootRecordOffset + 2, rootDirSector);
    writeLe32(pvd, rootRecordOffset + 10, rootDirSize);
    pvd[rootRecordOffset + 25] = 0x02;
    writeLe16(pvd, rootRecordOffset + 28, 1);
    pvd[rootRecordOffset + 32] = 1;
    pvd[rootRecordOffset + 33] = 0;
    writeMode2FormSector(image, dataTrackStart + 16, pvd, false);

    std::vector<uint8_t> svd(kSectorSize, 0);
    svd[0] = 2;
    std::memcpy(svd.data() + 1, "CD001", 5);
    svd[6] = 1;
    svd[88] = 0x25;
    svd[89] = 0x2F;
    svd[90] = 0x40;
    size_t jolietRootRecord = 156;
    svd[jolietRootRecord] = 34;
    writeLe32(svd, jolietRootRecord + 2, jolietRootSector);
    writeLe32(svd, jolietRootRecord + 10, kSectorSize);
    svd[jolietRootRecord + 25] = 0x02;
    writeLe16(svd, jolietRootRecord + 28, 1);
    svd[jolietRootRecord + 32] = 1;
    svd[jolietRootRecord + 33] = 0;
    writeMode2FormSector(image, dataTrackStart + 17, svd, false);

    std::vector<uint8_t> terminator(kSectorSize, 0);
    terminator[0] = 255;
    std::memcpy(terminator.data() + 1, "CD001", 5);
    terminator[6] = 1;
    writeMode2FormSector(image, dataTrackStart + 18, terminator, false);

    std::vector<uint8_t> rootDir(kSectorSize, 0);
    size_t cursor = 0;
    cursor += writeDirectoryRecord(rootDir, cursor, std::string("\0", 1), rootDirSector,
                                   rootDirSize, 0x02);
    cursor += writeDirectoryRecord(rootDir, cursor, std::string("\1", 1), rootDirSector,
                                   rootDirSize, 0x02);
    cursor += writeDirectoryRecord(rootDir, cursor, "SYSTEM.CNF;1", systemCnfSector, 40, 0x00);
    cursor += writeDirectoryRecord(rootDir, cursor, "GAME.EXE;1", exeSector, 16, 0x00);
    cursor +=
        writeDirectoryRecord(rootDir, cursor, "MULTI.BIN;1", multiExtentSectorA, kSectorSize, 0x80);
    cursor +=
        writeDirectoryRecord(rootDir, cursor, "MULTI.BIN;1", multiExtentSectorB, kSectorSize, 0x00);
    cursor += writeDirectoryRecord(rootDir, cursor, "AUDIO.XA;1", xaSector, 2324, 0x00);
    cursor += writeDirectoryRecord(rootDir, cursor, "TEXTURE.TIM;1", timSector, 128, 0x00);
    writeDirectoryRecord(rootDir, cursor, "MOVIE.STR;1", strSector, 256, 0x00);
    writeMode2FormSector(image, dataTrackStart + rootDirSector, rootDir, false);

    std::vector<uint8_t> jolietDir(kSectorSize, 0);
    size_t jolietCursor = 0;
    jolietCursor += writeJolietDirectoryRecord(jolietDir, jolietCursor, u"\0", jolietRootSector,
                                               kSectorSize, 0x02);
    jolietCursor += writeJolietDirectoryRecord(jolietDir, jolietCursor, u"\1", jolietRootSector,
                                               kSectorSize, 0x02);
    jolietCursor += writeJolietDirectoryRecord(jolietDir, jolietCursor, u"SYSTEM.CNF",
                                               systemCnfSector, 40, 0x00);
    jolietCursor +=
        writeJolietDirectoryRecord(jolietDir, jolietCursor, u"GAME.EXE", exeSector, 16, 0x00);
    jolietCursor += writeJolietDirectoryRecord(jolietDir, jolietCursor, u"MULTI.BIN",
                                               multiExtentSectorA, kSectorSize, 0x80);
    jolietCursor += writeJolietDirectoryRecord(jolietDir, jolietCursor, u"MULTI.BIN",
                                               multiExtentSectorB, kSectorSize, 0x00);
    jolietCursor +=
        writeJolietDirectoryRecord(jolietDir, jolietCursor, u"AUDIO.XA", xaSector, 2324, 0x00);
    jolietCursor +=
        writeJolietDirectoryRecord(jolietDir, jolietCursor, u"TEXTURE.TIM", timSector, 128, 0x00);
    jolietCursor +=
        writeJolietDirectoryRecord(jolietDir, jolietCursor, u"MOVIE.STR", strSector, 256, 0x00);
    writeJolietDirectoryRecord(jolietDir, jolietCursor, u"LONGNAME.TXT", systemCnfSector, 40, 0x00);
    writeMode2FormSector(image, dataTrackStart + jolietRootSector, jolietDir, false);

    std::string systemCnf = "BOOT = cdrom:\\GAME.EXE;1\n";
    std::vector<uint8_t> systemData(systemCnf.begin(), systemCnf.end());
    writeMode2FormSector(image, dataTrackStart + systemCnfSector, systemData, false);

    std::string exeData = "PS-X EXE";
    std::vector<uint8_t> exeBytes(exeData.begin(), exeData.end());
    writeMode2FormSector(image, dataTrackStart + exeSector, exeBytes, false);

    std::vector<uint8_t> multiA(kSectorSize, 'A');
    std::vector<uint8_t> multiB(kSectorSize, 'B');
    writeMode2FormSector(image, dataTrackStart + multiExtentSectorA, multiA, false);
    writeMode2FormSector(image, dataTrackStart + multiExtentSectorB, multiB, false);

    std::vector<uint8_t> xaData(2324, 'X');
    writeXaAudioSector(image, dataTrackStart + xaSector, xaData);

    std::vector<uint8_t> timData(128, 'T');
    writeMode2FormSector(image, dataTrackStart + timSector, timData, false);

    std::vector<uint8_t> strData(256, 'S');
    writeMode2FormSector(image, dataTrackStart + strSector, strData, false);

    std::ofstream binOut(binPath, std::ios::binary);
    binOut.write(reinterpret_cast<const char*>(image.data()),
                 static_cast<std::streamsize>(image.size()));
    binOut.close();

    std::ofstream cueOut(cuePath);
    cueOut << "FILE \"" << binPath.filename().string() << "\" BINARY\n";
    cueOut << "  TRACK 01 MODE2/2352\n";
    cueOut << "    INDEX 01 00:00:00\n";
    cueOut.close();

    return binPath;
}

inline std::filesystem::path createRawCueIso(const std::string& volumeLabel)
{
    const uint32_t totalSectors = 40;
    std::vector<uint8_t> image(totalSectors * kRawSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = kSectorSize;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeSector = 22;

    std::vector<uint8_t> pvd(kSectorSize, 0);
    pvd[0] = 1;
    std::memcpy(pvd.data() + 1, "CD001", 5);
    pvd[6] = 1;
    std::memcpy(pvd.data() + 8, "PLAYSTATION", 11);
    std::memcpy(pvd.data() + 40, volumeLabel.data(),
                std::min(volumeLabel.size(), static_cast<size_t>(31)));
    writeLe32(pvd, 80, totalSectors);
    writeLe16(pvd, 120, 1);
    writeLe16(pvd, 124, 1);
    writeLe16(pvd, 128, kSectorSize);
    size_t rootRecordOffset = 156;
    pvd[rootRecordOffset] = 34;
    writeLe32(pvd, rootRecordOffset + 2, rootDirSector);
    writeLe32(pvd, rootRecordOffset + 10, rootDirSize);
    pvd[rootRecordOffset + 25] = 0x02;
    writeLe16(pvd, rootRecordOffset + 28, 1);
    pvd[rootRecordOffset + 32] = 1;
    pvd[rootRecordOffset + 33] = 0;
    writeMode2FormSector(image, 16, pvd, false);

    std::vector<uint8_t> terminator(kSectorSize, 0);
    terminator[0] = 255;
    std::memcpy(terminator.data() + 1, "CD001", 5);
    terminator[6] = 1;
    writeMode2FormSector(image, 17, terminator, false);

    std::vector<uint8_t> rootDir(kSectorSize, 0);
    size_t cursor = 0;
    cursor += writeDirectoryRecord(rootDir, cursor, std::string("\0", 1), rootDirSector,
                                   rootDirSize, 0x02);
    cursor += writeDirectoryRecord(rootDir, cursor, std::string("\1", 1), rootDirSector,
                                   rootDirSize, 0x02);
    cursor += writeDirectoryRecord(rootDir, cursor, "SYSTEM.CNF;1", systemCnfSector, 40, 0x00);
    writeDirectoryRecord(rootDir, cursor, "GAME.EXE;1", exeSector, 16, 0x00);
    writeMode2FormSector(image, rootDirSector, rootDir, false);

    std::string systemCnf = "BOOT = cdrom:\\GAME.EXE;1\n";
    std::vector<uint8_t> systemData(systemCnf.begin(), systemCnf.end());
    writeMode2FormSector(image, systemCnfSector, systemData, false);

    std::string exeData = "PS-X EXE";
    std::vector<uint8_t> exeBytes(exeData.begin(), exeData.end());
    writeMode2FormSector(image, exeSector, exeBytes, false);

    auto uniqueSuffix = generateUniqueSuffix();
    auto binPath =
        std::filesystem::temp_directory_path() /
        ("psxrecomp_session_" + volumeLabel + "_" + std::to_string(uniqueSuffix) + ".bin");

    std::ofstream binOut(binPath, std::ios::binary);
    binOut.write(reinterpret_cast<const char*>(image.data()),
                 static_cast<std::streamsize>(image.size()));
    binOut.close();

    return binPath;
}

inline std::filesystem::path createMultiSessionCue(std::filesystem::path& cuePath,
                                                   std::filesystem::path& sessionTwoBin)
{
    auto sessionOneBin = createRawCueIso("SESSION_ONE");
    sessionTwoBin = createRawCueIso("SESSION_TWO");

    auto uniqueSuffix = generateUniqueSuffix();
    cuePath = std::filesystem::temp_directory_path() /
              ("psxrecomp_multisession_" + std::to_string(uniqueSuffix) + ".cue");
    std::ofstream cueOut(cuePath);
    cueOut << "REM SESSION 1\n";
    cueOut << "FILE \"" << sessionOneBin.filename().string() << "\" BINARY\n";
    cueOut << "  TRACK 01 MODE2/2352\n";
    cueOut << "    INDEX 01 00:02:00\n";
    cueOut << "REM SESSION 2\n";
    cueOut << "FILE \"" << sessionTwoBin.filename().string() << "\" BINARY\n";
    cueOut << "  TRACK 02 MODE2/2352\n";
    cueOut << "    PREGAP 00:02:00\n";
    cueOut << "    INDEX 01 00:02:00\n";
    cueOut.close();

    return sessionOneBin;
}

} // namespace iso_test
