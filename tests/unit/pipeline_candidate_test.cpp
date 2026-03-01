#include "psxrecomp/recompiler/pipeline.h"

#include "iso_test_helpers_base.h"
#include "psxrecomp/iso/psx_exe_loader.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

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

bool catalogEntryHasType(const std::string& catalog, const std::string& isoPath,
                         const std::string& type)
{
    const std::string isoMarker = "\"isoPath\": \"" + isoPath + "\"";
    const size_t isoPos = catalog.find(isoMarker);
    if (isoPos == std::string::npos)
    {
        return false;
    }

    const size_t nextIso = catalog.find("\"isoPath\": \"", isoPos + isoMarker.size());
    const std::string entrySlice =
        catalog.substr(isoPos, nextIso == std::string::npos ? std::string::npos : nextIso - isoPos);
    return entrySlice.find("\"" + type + "\"") != std::string::npos;
}

bool catalogEntryHasSha1(const std::string& catalog, const std::string& isoPath,
                         const std::string& sha1)
{
    const std::string isoMarker = "\"isoPath\": \"" + isoPath + "\"";
    const size_t isoPos = catalog.find(isoMarker);
    if (isoPos == std::string::npos)
    {
        return false;
    }

    const size_t nextIso = catalog.find("\"isoPath\": \"", isoPos + isoMarker.size());
    const std::string entrySlice =
        catalog.substr(isoPos, nextIso == std::string::npos ? std::string::npos : nextIso - isoPos);
    return entrySlice.find("\"sha1\": \"" + sha1 + "\"") != std::string::npos;
}

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
} // namespace

int main()
{
    auto isoPath = createIsoWithExecutables("DISC_BOOT", "BOOT = cdrom:\\GAMEB.EXE;1\n");
    auto tempDir = std::filesystem::temp_directory_path();
    auto outputDir = tempDir / "psxrecomp_candidates_out";

    psxrecomp::recompiler::PipelineOptions options;
    options.outputDirectory = outputDir.string();
    options.enableOptimizations = false;
    options.preserveSymbols = false;
    options.verbose = false;
    options.manifestTimestamp = "2024-01-01T00:00:00Z";

    psxrecomp::recompiler::RecompilationPipeline pipeline(options);
    auto result = pipeline.run(isoPath.string());
    assert(result.success);
    assert(result.selectionInfo.selectedPath == "GAMEB.EXE");
    assert(!result.artifacts.resourceManifestPath.empty());
    assert(std::filesystem::exists(result.artifacts.resourceManifestPath));
    assert(std::filesystem::exists(std::filesystem::path(result.artifacts.resourcesPath) / "index" /
                                   "disc_tree.json"));
    assert(std::filesystem::exists(std::filesystem::path(result.artifacts.resourcesPath) / "index" /
                                   "disc_meta.json"));
    assert(std::filesystem::exists(std::filesystem::path(result.artifacts.resourcesPath) / "index" /
                                   "recomp_inputs.json"));
    assert(std::filesystem::exists(std::filesystem::path(result.artifacts.resourcesPath) / "index" /
                                   "catalog.json"));
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/resources_manifest.json") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/disc_tree.json") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/disc_meta.json") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/recomp_inputs.json") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/catalog.json") != result.artifacts.exportedResources.end());
    {
        std::ifstream manifestFile(result.artifacts.resourceManifestPath);
        const std::string resourceManifest((std::istreambuf_iterator<char>(manifestFile)),
                                           std::istreambuf_iterator<char>());
        assert(resourceManifest.find("\"discTreePath\": \"index/disc_tree.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"discMetaPath\": \"index/disc_meta.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"recompInputsPath\": \"index/recomp_inputs.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"catalogPath\": \"index/catalog.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"filesystemEnabled\": true") != std::string::npos);
    }
    {
        std::ifstream recompInputsFile(std::filesystem::path(result.artifacts.resourcesPath) /
                                       "index" / "recomp_inputs.json");
        const std::string recompInputs((std::istreambuf_iterator<char>(recompInputsFile)),
                                       std::istreambuf_iterator<char>());
        assert(recompInputs.find("\"isoPath\": \"GAMEB.EXE\"") != std::string::npos);
        assert(recompInputs.find("\"exportedPath\": \"fs/GAMEB.EXE\"") != std::string::npos);
        assert(recompInputs.find("\"isoPath\": \"GAMEA.EXE\"") != std::string::npos);
        assert(recompInputs.find("\"exportedPath\": \"fs/GAMEA.EXE\"") != std::string::npos);
        assert(recompInputs.find("\"loadAddr\": \"0x80010000\"") != std::string::npos);
        assert(recompInputs.find("\"loadAddr\": \"0x80020000\"") != std::string::npos);
    }
    {
        std::ifstream catalogFile(std::filesystem::path(result.artifacts.resourcesPath) / "index" /
                                  "catalog.json");
        const std::string catalog((std::istreambuf_iterator<char>(catalogFile)),
                                  std::istreambuf_iterator<char>());
        assert(catalog.find("\"isoPath\": \"GAMEA.EXE\"") != std::string::npos);
        assert(catalog.find("\"isoPath\": \"GAMEB.EXE\"") != std::string::npos);
        assert(catalog.find("\"sha1\": \"") != std::string::npos);
        assert(
            catalogEntryHasSha1(catalog, "GAMEA.EXE", "c81ef78add2542090c4124b515612c2b5b42b5a2"));
        assert(
            catalogEntryHasSha1(catalog, "GAMEB.EXE", "071bd5165a34451b85d3c0835f990394eec29a44"));
        assert(catalogEntryHasType(catalog, "GAMEA.EXE", "psx_exe"));
        assert(catalogEntryHasType(catalog, "GAMEB.EXE", "psx_exe"));
    }
    {
        std::ifstream pipelineManifest(result.artifacts.manifestPath);
        const std::string pipelineManifestText((std::istreambuf_iterator<char>(pipelineManifest)),
                                               std::istreambuf_iterator<char>());
        assert(pipelineManifestText.find("\"resourceRoot\"") != std::string::npos);
        assert(pipelineManifestText.find("\"resourceManifest\"") != std::string::npos);
    }

    std::filesystem::remove(isoPath);

    auto isoPathSorted = createIsoWithExecutables("DISC_SORT", "");
    auto resultSorted = pipeline.run(isoPathSorted.string());
    assert(resultSorted.success);
    assert(resultSorted.selectionInfo.selectedPath == "GAMEA.EXE");
    std::filesystem::remove(isoPathSorted);

    auto policyIsoPath = createIsoForExportPolicy("DISC_POLICY");
    auto makeOptions = [&]()
    {
        psxrecomp::recompiler::PipelineOptions modeOptions = options;
        modeOptions.outputDirectory = outputDir.string();
        modeOptions.enableOptimizations = false;
        modeOptions.preserveSymbols = false;
        modeOptions.verbose = false;
        modeOptions.manifestTimestamp = "2024-01-01T00:00:00Z";
        return modeOptions;
    };
    auto countFsExports = [](const psxrecomp::recompiler::PipelineResult& runResult)
    {
        return std::count_if(runResult.artifacts.exportedResources.begin(),
                             runResult.artifacts.exportedResources.end(),
                             [](const std::string& path) { return path.rfind("fs/", 0) == 0; });
    };
    auto hasExportedPath =
        [](const psxrecomp::recompiler::PipelineResult& runResult, const std::string& path)
    {
        return std::find(runResult.artifacts.exportedResources.begin(),
                         runResult.artifacts.exportedResources.end(),
                         path) != runResult.artifacts.exportedResources.end();
    };

    auto minimalOptions = makeOptions();
    minimalOptions.resourceExport.fsMode =
        psxrecomp::recompiler::PipelineOptions::ResourceExportOptions::FsMode::Minimal;
    psxrecomp::recompiler::RecompilationPipeline minimalPipeline(minimalOptions);
    const auto minimalResult = minimalPipeline.run(policyIsoPath.string());
    assert(minimalResult.success);

    auto smartOptions = makeOptions();
    smartOptions.resourceExport.fsMode =
        psxrecomp::recompiler::PipelineOptions::ResourceExportOptions::FsMode::Smart;
    smartOptions.resourceExport.maxSingleFileBytes = 2048;
    smartOptions.resourceExport.maxTotalBytes = 16384;
    psxrecomp::recompiler::RecompilationPipeline smartPipeline(smartOptions);
    const auto smartResult = smartPipeline.run(policyIsoPath.string());
    assert(smartResult.success);

    auto fullOptions = makeOptions();
    fullOptions.resourceExport.fsMode =
        psxrecomp::recompiler::PipelineOptions::ResourceExportOptions::FsMode::Full;
    psxrecomp::recompiler::RecompilationPipeline fullPipeline(fullOptions);
    const auto fullResult = fullPipeline.run(policyIsoPath.string());
    assert(fullResult.success);

    const size_t minimalFsCount = countFsExports(minimalResult);
    const size_t smartFsCount = countFsExports(smartResult);
    const size_t fullFsCount = countFsExports(fullResult);
    assert(minimalFsCount == 3);
    assert(minimalFsCount < smartFsCount);
    assert(smartFsCount < fullFsCount);

    assert(hasExportedPath(minimalResult, "fs/SYSTEM.CNF"));
    assert(hasExportedPath(minimalResult, "fs/GAMEB.EXE"));
    assert(hasExportedPath(minimalResult, "fs/GAMEA.EXE"));
    assert(hasExportedPath(smartResult, "fs/SYSTEM.CNF"));
    assert(hasExportedPath(smartResult, "fs/GAMEB.EXE"));
    assert(hasExportedPath(fullResult, "fs/SYSTEM.CNF"));
    assert(hasExportedPath(fullResult, "fs/GAMEB.EXE"));

    std::filesystem::remove(policyIsoPath);

    auto timIsoPath = createIsoWithTimBin("DISC_TIM_BIN");
    auto timResult = pipeline.run(timIsoPath.string());
    assert(timResult.success);
    {
        std::ifstream catalogFile(std::filesystem::path(timResult.artifacts.resourcesPath) /
                                  "index" / "catalog.json");
        const std::string catalog((std::istreambuf_iterator<char>(catalogFile)),
                                  std::istreambuf_iterator<char>());
        assert(catalog.find("\"isoPath\": \"TEXTURE.BIN\"") != std::string::npos);
        assert(catalogEntryHasType(catalog, "TEXTURE.BIN", "tim"));
    }
    std::filesystem::remove(timIsoPath);

    std::error_code error;
    std::filesystem::remove_all(outputDir, error);
    return 0;
}
