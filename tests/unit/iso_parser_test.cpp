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
constexpr uint32_t kRawSectorSize = 2352;
constexpr uint32_t kMode2DataOffset = 24;

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

size_t writeJolietDirectoryRecord(std::vector<uint8_t>& buffer, size_t offset,
                                  const std::u16string& name, uint32_t extent, uint32_t size,
                                  uint8_t flags)
{
    uint8_t nameLength = static_cast<uint8_t>(name.size() * 2);
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
    for (size_t i = 0; i < name.size(); ++i)
    {
        buffer[offset + 33 + (i * 2)] = static_cast<uint8_t>((name[i] >> 8) & 0xFF);
        buffer[offset + 33 + (i * 2) + 1] = static_cast<uint8_t>(name[i] & 0xFF);
    }
    return recordLength;
}

void writeMode2Sector(std::vector<uint8_t>& image, uint32_t lba, const std::vector<uint8_t>& data,
                      bool form2)
{
    size_t offset = static_cast<size_t>(lba) * kRawSectorSize;
    image[offset + 15] = 2;
    image[offset + 18] = form2 ? 0x20 : 0x00;
    size_t copySize = form2 ? 2324 : 2048;
    std::memcpy(image.data() + offset + kMode2DataOffset, data.data(),
                std::min(copySize, data.size()));
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

std::filesystem::path createCueImage(std::filesystem::path& cuePath)
{
    const uint32_t dataTrackStart = 150;
    const uint32_t totalSectors = 180;
    std::vector<uint8_t> image(totalSectors * kRawSectorSize, 0);

    const uint32_t rootDirSector = 20;
    const uint32_t rootDirSize = kSectorSize;
    const uint32_t systemCnfSector = 21;
    const uint32_t exeSector = 22;
    const uint32_t multiExtentSectorA = 23;
    const uint32_t multiExtentSectorB = 24;
    const uint32_t xaSector = 25;
    const uint32_t jolietRootSector = 26;

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
    writeMode2Sector(image, dataTrackStart + 16, pvd, false);

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
    writeMode2Sector(image, dataTrackStart + 17, svd, false);

    std::vector<uint8_t> terminator(kSectorSize, 0);
    terminator[0] = 255;
    std::memcpy(terminator.data() + 1, "CD001", 5);
    terminator[6] = 1;
    writeMode2Sector(image, dataTrackStart + 18, terminator, false);

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
    cursor += writeDirectoryRecord(rootDir, cursor, "XA.DAT;1", xaSector, 2324, 0x00);
    writeMode2Sector(image, dataTrackStart + rootDirSector, rootDir, false);

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
        writeJolietDirectoryRecord(jolietDir, jolietCursor, u"XA.DAT", xaSector, 2324, 0x00);
    jolietCursor += writeJolietDirectoryRecord(jolietDir, jolietCursor, u"LONGNAME.TXT",
                                               systemCnfSector, 40, 0x00);
    writeMode2Sector(image, dataTrackStart + jolietRootSector, jolietDir, false);

    std::string systemCnf = "BOOT = cdrom:\\GAME.EXE;1\n";
    std::vector<uint8_t> systemData(systemCnf.begin(), systemCnf.end());
    writeMode2Sector(image, dataTrackStart + systemCnfSector, systemData, false);

    std::string exeData = "PS-X EXE";
    std::vector<uint8_t> exeBytes(exeData.begin(), exeData.end());
    writeMode2Sector(image, dataTrackStart + exeSector, exeBytes, false);

    std::vector<uint8_t> multiA(kSectorSize, 'A');
    std::vector<uint8_t> multiB(kSectorSize, 'B');
    writeMode2Sector(image, dataTrackStart + multiExtentSectorA, multiA, false);
    writeMode2Sector(image, dataTrackStart + multiExtentSectorB, multiB, false);

    std::vector<uint8_t> xaData(2324, 'X');
    writeMode2Sector(image, dataTrackStart + xaSector, xaData, true);

    auto binPath = std::filesystem::temp_directory_path() / "psxrecomp_test.bin";
    std::ofstream binOut(binPath, std::ios::binary);
    binOut.write(reinterpret_cast<const char*>(image.data()),
                 static_cast<std::streamsize>(image.size()));
    binOut.close();

    cuePath = std::filesystem::temp_directory_path() / "psxrecomp_test.cue";
    std::ofstream cueOut(cuePath);
    cueOut << "FILE \"" << binPath.filename().string() << "\" BINARY\n";
    cueOut << "  TRACK 01 MODE2/2352\n";
    cueOut << "    INDEX 01 00:02:00\n";
    cueOut.close();

    return binPath;
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

    std::filesystem::path cuePath;
    auto binPath = createCueImage(cuePath);
    psxrecomp::iso::IsoParser cueParser(cuePath.string());
    assert(cueParser.open());
    assert(cueParser.isValid());
    assert(cueParser.findExecutable() == "GAME.EXE");

    auto multiExtent = cueParser.extractFile("MULTI.BIN");
    assert(multiExtent.size() == kSectorSize * 2);
    assert(multiExtent.front() == 'A');
    assert(multiExtent.back() == 'B');

    auto xaData = cueParser.extractFile("XA.DAT");
    assert(xaData.size() == 2324);
    assert(xaData.front() == 'X');

    auto jolietData = cueParser.extractFile("LONGNAME.TXT");
    assert(!jolietData.empty());

    std::filesystem::remove(cuePath);
    std::filesystem::remove(binPath);
    return 0;
}
