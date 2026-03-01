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
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/resources_manifest.json") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/disc_tree.json") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/disc_meta.json") != result.artifacts.exportedResources.end());
    {
        std::ifstream manifestFile(result.artifacts.resourceManifestPath);
        const std::string resourceManifest((std::istreambuf_iterator<char>(manifestFile)),
                                           std::istreambuf_iterator<char>());
        assert(resourceManifest.find("\"discTreePath\": \"index/disc_tree.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"discMetaPath\": \"index/disc_meta.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"filesystemEnabled\": true") != std::string::npos);
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
    std::error_code error;
    std::filesystem::remove_all(outputDir, error);
    return 0;
}
