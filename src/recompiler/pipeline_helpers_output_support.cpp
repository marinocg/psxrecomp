#include "pipeline_helpers_output_support.h"

#include "pipeline_helpers_output_model.h"
#include "pipeline_helpers_output_support_internal.h"
#include "pipeline_selection_helpers.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{

constexpr u32 kDiscUserSectorSize = 2048;
constexpr u32 kDiscRawSectorSize = 2352;

u32 bytesToSectors(u64 byteCount, u32 sectorSize)
{
    if (sectorSize == 0 || byteCount == 0)
    {
        return 0;
    }
    return static_cast<u32>((byteCount + static_cast<u64>(sectorSize) - 1ULL) /
                            static_cast<u64>(sectorSize));
}

std::string discBlobModeToString(PipelineOptions::ResourceExportOptions::DiscBlobMode mode)
{
    switch (mode)
    {
    case PipelineOptions::ResourceExportOptions::DiscBlobMode::Force2048:
        return "force2048";
    case PipelineOptions::ResourceExportOptions::DiscBlobMode::Force2352:
        return "force2352";
    case PipelineOptions::ResourceExportOptions::DiscBlobMode::Disabled:
        return "disabled";
    case PipelineOptions::ResourceExportOptions::DiscBlobMode::Auto:
    default:
        return "auto";
    }
}

std::vector<DiscLayoutFile> buildDiscLayoutFiles(const std::vector<iso::IsoFileEntry>& entries,
                                                 u32 logicalBlockSize)
{
    std::vector<DiscLayoutFile> files;
    files.reserve(entries.size());

    for (const auto& entry : entries)
    {
        if (entry.isDirectory)
        {
            continue;
        }

        DiscLayoutFile file;
        file.path = entry.path;
        file.bytes = static_cast<u64>(entry.size);

        std::vector<iso::IsoFileExtent> sortedExtents = entry.extents;
        std::sort(sortedExtents.begin(), sortedExtents.end(),
                  [](const iso::IsoFileExtent& lhs, const iso::IsoFileExtent& rhs)
                  {
                      if (lhs.lba != rhs.lba)
                      {
                          return lhs.lba < rhs.lba;
                      }
                      if (lhs.size != rhs.size)
                      {
                          return lhs.size < rhs.size;
                      }
                      return lhs.continues && !rhs.continues;
                  });

        u64 fileByteOffset = 0;
        file.extents.reserve(sortedExtents.size());
        for (const auto& extent : sortedExtents)
        {
            DiscLayoutExtent layoutExtent;
            layoutExtent.lba = extent.lba;
            layoutExtent.sectors = bytesToSectors(static_cast<u64>(extent.size), logicalBlockSize);
            layoutExtent.fileByteOffset = fileByteOffset;
            file.extents.push_back(layoutExtent);
            fileByteOffset += static_cast<u64>(extent.size);
        }

        files.push_back(std::move(file));
    }

    std::sort(files.begin(), files.end(),
              [](const DiscLayoutFile& lhs, const DiscLayoutFile& rhs)
              {
                  const std::string lhsKey = toLower(lhs.path);
                  const std::string rhsKey = toLower(rhs.path);
                  if (lhsKey != rhsKey)
                  {
                      return lhsKey < rhsKey;
                  }
                  return lhs.path < rhs.path;
              });
    return files;
}

struct DiscLbaRange
{
    u32 start = 0;
    u32 count = 0;
};

bool determineDataTrackLbaRange(const iso::IsoParser& parser, DiscLbaRange& outRange,
                                std::string& outError)
{
    outRange = DiscLbaRange{};
    outError.clear();

    const auto& tracks = parser.getTracks();
    if (tracks.empty())
    {
        const u32 volumeCount = parser.getVolumeSpaceSize();
        if (volumeCount == 0)
        {
            outError = "Disc blob export failed: volume space size is zero.";
            return false;
        }
        outRange.start = 0;
        outRange.count = volumeCount;
        return true;
    }

    const auto dataTrack = parser.getDataTrack();
    if (!dataTrack.has_value())
    {
        outError = "Disc blob export failed: no data track found.";
        return false;
    }

    outRange.start = dataTrack->startLba;
    u32 nextTrackStart = std::numeric_limits<u32>::max();
    for (const auto& track : tracks)
    {
        if (track.startLba > outRange.start && track.startLba < nextTrackStart)
        {
            nextTrackStart = track.startLba;
        }
    }
    if (nextTrackStart != std::numeric_limits<u32>::max() && nextTrackStart > outRange.start)
    {
        outRange.count = nextTrackStart - outRange.start;
        return true;
    }

    const u32 volumeCount = parser.getVolumeSpaceSize();
    if (volumeCount != 0)
    {
        outRange.count = volumeCount;
        return true;
    }

    const u32 totalSectors = parser.getTotalSectors();
    if (totalSectors > outRange.start)
    {
        outRange.count = totalSectors - outRange.start;
        return true;
    }

    outError = "Disc blob export failed: unable to determine data-track LBA range.";
    return false;
}

bool readBlobSector(iso::IsoParser& parser, u32 relativeLba, u32 sectorSize, std::vector<u8>& out,
                    std::string& outError)
{
    out.clear();
    outError.clear();

    if (sectorSize == kDiscRawSectorSize)
    {
        if (!parser.readSectorRaw2352(relativeLba, out))
        {
            outError = parser.getLastError();
            return false;
        }
    }
    else if (sectorSize == kDiscUserSectorSize)
    {
        if (!parser.readSectorUser2048(relativeLba, out))
        {
            outError = parser.getLastError();
            return false;
        }
    }
    else
    {
        outError = "Unsupported disc blob sector size.";
        return false;
    }

    if (out.size() != sectorSize)
    {
        outError = "Unexpected sector size returned by parser.";
        out.clear();
        return false;
    }
    return true;
}

bool exportDiscDataTrackArtifacts(const std::filesystem::path& resourcesRoot,
                                  iso::IsoParser& parser,
                                  const std::vector<iso::IsoFileEntry>& isoTreeEntries,
                                  const PipelineOptions::ResourceExportOptions& resourceOptions,
                                  std::vector<std::string>& warnings,
                                  std::vector<std::string>& manifestWarnings,
                                  PipelineArtifacts& artifacts, DiscBlobSummary& outSummary,
                                  std::string& outError)
{
    outSummary = DiscBlobSummary{};
    outSummary.enabled = resourceOptions.discBlob.enabled;
    outSummary.mode = discBlobModeToString(resourceOptions.discBlob.mode);
    outSummary.maxBytes = resourceOptions.discBlob.maxBytes;

    std::error_code dirError;
    const std::filesystem::path discRoot = resourcesRoot / "disc";
    std::filesystem::remove_all(discRoot, dirError);
    if (dirError)
    {
        outError = "Failed to clear disc resources directory: " + discRoot.string() + ": " +
                   dirError.message();
        return false;
    }

    if (resourceOptions.discBlob.mode ==
        PipelineOptions::ResourceExportOptions::DiscBlobMode::Disabled)
    {
        outSummary.enabled = false;
        return true;
    }

    if (!resourceOptions.discBlob.enabled)
    {
        return true;
    }

    std::filesystem::create_directories(discRoot, dirError);
    if (dirError)
    {
        outError = "Failed to create disc resources directory: " + discRoot.string() + ": " +
                   dirError.message();
        return false;
    }

    outSummary.sectorSize = kDiscUserSectorSize;
    if (resourceOptions.discBlob.mode ==
        PipelineOptions::ResourceExportOptions::DiscBlobMode::Force2352)
    {
        if (!parser.canReadRaw2352())
        {
            outError = "Disc blob export failed: force2352 requested but raw 2352 sectors are "
                       "unavailable.";
            return false;
        }
        outSummary.sectorSize = kDiscRawSectorSize;
    }
    else if (resourceOptions.discBlob.mode ==
             PipelineOptions::ResourceExportOptions::DiscBlobMode::Force2048)
    {
        if (!parser.canReadUser2048())
        {
            outError = "Disc blob export failed: force2048 requested but 2048 user sectors are "
                       "unavailable.";
            return false;
        }
        outSummary.sectorSize = kDiscUserSectorSize;
    }
    else
    {
        outSummary.sectorSize = parser.canReadRaw2352() ? kDiscRawSectorSize : kDiscUserSectorSize;
    }

    if (outSummary.sectorSize == kDiscRawSectorSize)
    {
        outSummary.format = "data_track_raw_2352";
    }
    else
    {
        outSummary.format = "data_track_user_2048";
    }

    DiscLbaRange lbaRange{};
    if (!determineDataTrackLbaRange(parser, lbaRange, outError))
    {
        return false;
    }
    outSummary.lbaStart = lbaRange.start;

    u32 exportSectorCount = lbaRange.count;
    if (resourceOptions.discBlob.maxBytes > 0)
    {
        const u64 capSectors64 =
            resourceOptions.discBlob.maxBytes / static_cast<u64>(outSummary.sectorSize);
        if (capSectors64 == 0)
        {
            outError = "Disc blob export failed: maxBytes is smaller than one output sector.";
            return false;
        }
        const u32 capSectors = static_cast<u32>(
            std::min<u64>(capSectors64, static_cast<u64>(std::numeric_limits<u32>::max())));
        if (capSectors < exportSectorCount)
        {
            exportSectorCount = capSectors;
            outSummary.truncated = true;
            const std::string warning =
                "Disc blob export was truncated by maxBytes cap (lbaCount=" +
                std::to_string(exportSectorCount) + " of " + std::to_string(lbaRange.count) + ").";
            warnings.push_back(warning);
            manifestWarnings.push_back(warning);
        }
    }
    outSummary.lbaCount = exportSectorCount;

    const std::filesystem::path blobHostPath = discRoot / "data_track.bin";
    std::ofstream blobStream(blobHostPath, std::ios::binary | std::ios::trunc);
    if (!blobStream)
    {
        outError = "Failed to create disc blob file: " + blobHostPath.string();
        return false;
    }

    Sha1Hasher hasher;
    std::vector<u8> sectorData;
    sectorData.reserve(outSummary.sectorSize);
    std::string readError;

    for (u32 offset = 0; offset < exportSectorCount; ++offset)
    {
        const u32 absoluteLba = outSummary.lbaStart + offset;
        const u32 relativeLba = absoluteLba - lbaRange.start;
        if (!readBlobSector(parser, relativeLba, outSummary.sectorSize, sectorData, readError))
        {
            outError =
                "Disc blob export failed at LBA " + std::to_string(absoluteLba) + ": " + readError;
            return false;
        }

        blobStream.write(reinterpret_cast<const char*>(sectorData.data()),
                         static_cast<std::streamsize>(sectorData.size()));
        if (!blobStream.good())
        {
            outError =
                "Disc blob export failed while writing LBA " + std::to_string(absoluteLba) + ".";
            return false;
        }
        hasher.update(sectorData.data(), sectorData.size());
        outSummary.blobBytes += static_cast<u64>(sectorData.size());
    }

    blobStream.flush();
    if (!blobStream.good())
    {
        outError = "Disc blob export failed while flushing output file.";
        return false;
    }

    outSummary.blobSha1 = sha1DigestToHex(hasher.finalize());

    const auto discLayoutFiles = buildDiscLayoutFiles(isoTreeEntries, parser.getLogicalBlockSize());
    const std::filesystem::path layoutHostPath = discRoot / "disc_layout.json";
    const std::filesystem::path hashesHostPath = discRoot / "disc_hashes.json";
    if (!writeFile(layoutHostPath, serializeDiscLayout(parser, outSummary, discLayoutFiles),
                   outError))
    {
        return false;
    }
    if (!writeFile(hashesHostPath, serializeDiscHashes(outSummary), outError))
    {
        return false;
    }

    const std::string blobResourcePath =
        (std::filesystem::path("disc") / "data_track.bin").generic_string();
    const std::string layoutResourcePath =
        (std::filesystem::path("disc") / "disc_layout.json").generic_string();
    const std::string hashesResourcePath =
        (std::filesystem::path("disc") / "disc_hashes.json").generic_string();
    artifacts.exportedResources.push_back(blobResourcePath);
    artifacts.exportedResources.push_back(layoutResourcePath);
    artifacts.exportedResources.push_back(hashesResourcePath);
    outSummary.blobPath = blobResourcePath;
    outSummary.layoutPath = layoutResourcePath;
    outSummary.hashesPath = hashesResourcePath;

    return true;
}

} // namespace

bool exportIsoResourceArtifacts(PipelineArtifacts& artifacts, const std::string& activeDiscPath,
                                const PipelineOptions::ResourceExportOptions& resourceExportOptions,
                                std::vector<std::string>& warnings,
                                std::vector<PipelineDiagnostic>& diagnostics,
                                const std::string& timestamp, const std::string& pipelineVersion,
                                std::string& outError)
{
    if (!ensureSha1SelfTest(outError))
    {
        return false;
    }

    iso::IsoParser parser(activeDiscPath);
    if (!(parser.open() && parser.isValid()))
    {
        for (const auto& error : parser.getErrors())
        {
            PipelineDiagnostic entry;
            entry.code = "IsoParserError";
            entry.severity = "error";
            entry.message = "Resource export skipped: " + error;
            entry.context.file = activeDiscPath;
            diagnostics.push_back(entry);
        }
        warnings.push_back("Resource export skipped due to parser errors.");
        return true;
    }

    std::error_code dirError;
    const std::filesystem::path resourcesRoot(artifacts.resourcesPath);
    const std::filesystem::path resourcesIndex = resourcesRoot / "index";
    const std::filesystem::path resourcesFs = resourcesRoot / "fs";

    std::filesystem::create_directories(resourcesIndex, dirError);
    if (dirError)
    {
        outError = "Failed to create resources index directory: " + resourcesIndex.string() + ": " +
                   dirError.message();
        return false;
    }
    std::filesystem::remove_all(resourcesFs, dirError);
    if (dirError)
    {
        outError = "Failed to clear filesystem resources directory: " + resourcesFs.string() +
                   ": " + dirError.message();
        return false;
    }
    std::filesystem::create_directories(resourcesFs, dirError);
    if (dirError)
    {
        outError = "Failed to create filesystem resources directory: " + resourcesFs.string() +
                   ": " + dirError.message();
        return false;
    }

    const auto isoTreeEntries = parser.listAllFilesRecursive();
    const auto bootExecutable = parser.findExecutable();
    const auto discTreePath = resourcesIndex / "disc_tree.json";
    const auto discMetaPath = resourcesIndex / "disc_meta.json";
    if (!writeFile(discTreePath, serializeDiscTree(isoTreeEntries), outError))
    {
        return false;
    }
    if (!writeFile(discMetaPath,
                   serializeDiscMeta(activeDiscPath, bootExecutable, parser, isoTreeEntries),
                   outError))
    {
        return false;
    }
    artifacts.exportedResources.push_back("index/disc_tree.json");
    artifacts.exportedResources.push_back("index/disc_meta.json");

    DiscBlobSummary discBlobSummary;
    std::vector<std::string> resourceManifestWarnings;
    if (!exportDiscDataTrackArtifacts(resourcesRoot, parser, isoTreeEntries, resourceExportOptions,
                                      warnings, resourceManifestWarnings, artifacts,
                                      discBlobSummary, outError))
    {
        return false;
    }

    FilesystemExportSummary filesystemSummary;
    filesystemSummary.mode = fsModeToString(resourceExportOptions.fsMode);
    filesystemSummary.maxTotalBytes = resourceExportOptions.maxTotalBytes;
    filesystemSummary.maxSingleFileBytes = resourceExportOptions.maxSingleFileBytes;
    std::unordered_map<std::string, std::string> exportedPathByIsoPath;
    std::unordered_map<std::string, const iso::IsoFileEntry*> fileEntryByPath;
    for (const auto& entry : isoTreeEntries)
    {
        if (entry.isDirectory)
        {
            continue;
        }
        fileEntryByPath.emplace(toLower(entry.path), &entry);
    }

    std::vector<std::string> executablePaths = parser.listExecutables();
    if (!bootExecutable.empty())
    {
        executablePaths.push_back(bootExecutable);
    }
    const std::vector<std::string> uniqueExecutablePaths =
        deduplicatePathsCaseInsensitive(executablePaths);
    std::unordered_set<std::string> executablePathSet;
    for (const auto& executablePath : uniqueExecutablePaths)
    {
        executablePathSet.insert(toLower(executablePath));
    }

    const FilesystemExportPlan exportPlan =
        buildFilesystemExportPlan(isoTreeEntries, bootExecutable, resourceExportOptions);
    filesystemSummary.skippedDueToLimits = exportPlan.skippedDueToLimits;
    filesystemSummary.alwaysIncludedRules = exportPlan.alwaysIncludedRules;

    struct ExportedFsFile
    {
        std::string isoPath;
        std::string exportedPath;
    };
    std::vector<ExportedFsFile> exportedFsFiles;
    exportedFsFiles.reserve(exportPlan.paths.size());

    for (const auto& isoRelativePath : exportPlan.paths)
    {
        std::string exportError;
        const std::filesystem::path destination =
            resourcesFs / std::filesystem::path(isoRelativePath);
        if (!parser.exportFileTo(isoRelativePath, destination, &exportError))
        {
            const std::string warning = "Failed to export ISO file '" + isoRelativePath +
                                        "' to resources/fs: " + exportError;
            warnings.push_back(warning);
            resourceManifestWarnings.push_back(warning);
            continue;
        }

        const std::string exportedPath =
            (std::filesystem::path("fs") / std::filesystem::path(isoRelativePath)).generic_string();
        artifacts.exportedResources.push_back(exportedPath);
        exportedPathByIsoPath.emplace(toLower(isoRelativePath), exportedPath);
        exportedFsFiles.push_back({isoRelativePath, exportedPath});
        ++filesystemSummary.filesExported;
        const auto entryIt = fileEntryByPath.find(toLower(isoRelativePath));
        if (entryIt != fileEntryByPath.end())
        {
            filesystemSummary.bytesExported += static_cast<u64>(entryIt->second->size);
        }
    }

    if (filesystemSummary.filesExported == 0)
    {
        const std::string warning =
            "Filesystem export produced no files. Check export policy and ISO contents.";
        warnings.push_back(warning);
        resourceManifestWarnings.push_back(warning);
    }

    auto findExportedPath = [&](const std::string& isoPath) -> std::string
    {
        if (isoPath.empty())
        {
            return "";
        }
        const auto it = exportedPathByIsoPath.find(toLower(isoPath));
        if (it == exportedPathByIsoPath.end())
        {
            return "";
        }
        return it->second;
    };

    std::vector<RecompInputExecutable> recompInputExecutables;
    recompInputExecutables.reserve(uniqueExecutablePaths.size());

    for (const auto& executablePath : uniqueExecutablePaths)
    {
        const std::vector<u8> executableData = parser.extractFile(executablePath);
        if (executableData.empty())
        {
            const std::string warning = "Recomp inputs metadata skipped for executable '" +
                                        executablePath + "' because extraction failed.";
            warnings.push_back(warning);
            resourceManifestWarnings.push_back(warning);
            continue;
        }

        iso::PsxExeHeader executableHeader{};
        iso::PsxExeDiagnostics executableDiagnostics;
        if (!iso::PsxExeLoader::parseHeader(executableData, executableHeader,
                                            &executableDiagnostics))
        {
            const std::string warning = "Recomp inputs metadata skipped for executable '" +
                                        executablePath + "' because PS-X EXE parsing failed.";
            warnings.push_back(warning);
            resourceManifestWarnings.push_back(warning);
            continue;
        }

        u32 effectiveLoadSize = executableHeader.loadSize;
        if (effectiveLoadSize == 0 && executableData.size() >= iso::PsxExeLoader::kHeaderSize)
        {
            effectiveLoadSize =
                static_cast<u32>(executableData.size() - iso::PsxExeLoader::kHeaderSize);
        }

        RecompInputExecutable entry;
        entry.isoPath = executablePath;
        entry.exportedPath = findExportedPath(executablePath);
        entry.loadAddress = executableHeader.loadAddress;
        entry.loadSize = effectiveLoadSize;
        entry.entryPoint = executableHeader.initialPc;
        entry.gp = executableHeader.initialGp;
        entry.bssAddress = executableHeader.bssAddress;
        entry.bssSize = executableHeader.bssSize;
        entry.stackAddress = executableHeader.stackAddress;
        entry.stackSize = executableHeader.stackSize;
        recompInputExecutables.push_back(std::move(entry));
    }

    std::sort(recompInputExecutables.begin(), recompInputExecutables.end(),
              [](const RecompInputExecutable& lhs, const RecompInputExecutable& rhs)
              {
                  const std::string lhsKey = toLower(lhs.isoPath);
                  const std::string rhsKey = toLower(rhs.isoPath);
                  if (lhsKey != rhsKey)
                  {
                      return lhsKey < rhsKey;
                  }
                  return lhs.isoPath < rhs.isoPath;
              });

    const auto recompInputsPath = resourcesIndex / "recomp_inputs.json";
    if (!writeFile(recompInputsPath,
                   serializeRecompInputs(bootExecutable, findExportedPath(bootExecutable),
                                         findExportedPath("SYSTEM.CNF"), recompInputExecutables),
                   outError))
    {
        return false;
    }
    artifacts.exportedResources.push_back("index/recomp_inputs.json");

    EmbeddedScanSummary embeddedScanSummary;
    std::vector<EmbeddedHit> embeddedHits;
    if (!scanEmbeddedTimResources(parser, isoTreeEntries, resourcesRoot, resourceExportOptions,
                                  warnings, resourceManifestWarnings, embeddedScanSummary,
                                  embeddedHits, outError))
    {
        return false;
    }

    std::vector<CatalogEntry> catalogEntries;
    catalogEntries.reserve(exportedFsFiles.size() + embeddedHits.size());
    const bool hasRawXaMetadata = parser.getRawSectorSize() == 2352;
    for (const auto& exportedFile : exportedFsFiles)
    {
        const auto fileEntryIt = fileEntryByPath.find(toLower(exportedFile.isoPath));
        if (fileEntryIt == fileEntryByPath.end())
        {
            outError = "Catalog generation failed: missing ISO tree entry for '" +
                       exportedFile.isoPath + "'.";
            return false;
        }

        std::string sha1;
        u64 exportedSize = 0;
        std::vector<u8> prefix;
        std::string hashError;
        const std::filesystem::path exportedHostPath = resourcesRoot / exportedFile.exportedPath;
        if (!computeSha1AndPrefix(exportedHostPath, sha1, exportedSize, prefix, hashError))
        {
            outError = "Catalog generation failed for '" + exportedFile.isoPath + "': " + hashError;
            return false;
        }

        CatalogEntry entry;
        entry.id = "discfile:" + exportedFile.isoPath;
        entry.sourceKind = "disc_file";
        entry.isoPath = exportedFile.isoPath;
        entry.exportedPath = exportedFile.exportedPath;
        entry.size = exportedSize;
        entry.sha1 = sha1;
        entry.extents = fileEntryIt->second->extents;
        entry.detectedTypes = detectCatalogTypes(
            exportedFile.isoPath, prefix, exportedSize, hasRawXaMetadata && !entry.extents.empty(),
            executablePathSet.find(toLower(exportedFile.isoPath)) != executablePathSet.end());
        catalogEntries.push_back(std::move(entry));
    }

    for (const auto& embeddedHit : embeddedHits)
    {
        CatalogEntry entry;
        entry.id =
            "embedded:" + embeddedHit.containerIsoPath + ":" + formatHex(embeddedHit.offset, 8);
        entry.sourceKind = "embedded";
        entry.isoPath = embeddedHit.containerIsoPath;
        entry.exportedPath = embeddedHit.outputPath;
        entry.size = embeddedHit.size;
        entry.sha1 = embeddedHit.sha1;
        entry.containerIsoPath = embeddedHit.containerIsoPath;
        entry.containerOffset = embeddedHit.offset;
        entry.detectedTypes = {"tim"};
        catalogEntries.push_back(std::move(entry));
        artifacts.exportedResources.push_back(embeddedHit.outputPath);
    }

    std::sort(catalogEntries.begin(), catalogEntries.end(),
              [](const CatalogEntry& lhs, const CatalogEntry& rhs)
              {
                  if (lhs.sourceKind != rhs.sourceKind)
                  {
                      return lhs.sourceKind < rhs.sourceKind;
                  }
                  const std::string lhsIso = toLower(lhs.isoPath);
                  const std::string rhsIso = toLower(rhs.isoPath);
                  if (lhsIso != rhsIso)
                  {
                      return lhsIso < rhsIso;
                  }
                  if (lhs.containerOffset != rhs.containerOffset)
                  {
                      return lhs.containerOffset < rhs.containerOffset;
                  }
                  return lhs.exportedPath < rhs.exportedPath;
              });

    const auto catalogPath = resourcesIndex / "catalog.json";
    if (!writeFile(catalogPath, serializeCatalog(catalogEntries), outError))
    {
        return false;
    }
    artifacts.exportedResources.push_back("index/catalog.json");

    const auto resourcesManifestPath = resourcesIndex / "resources_manifest.json";
    if (!writeFile(resourcesManifestPath,
                   serializeResourcesManifest(activeDiscPath, timestamp, pipelineVersion, parser,
                                              isoTreeEntries, filesystemSummary, discBlobSummary,
                                              embeddedScanSummary, resourceManifestWarnings),
                   outError))
    {
        return false;
    }
    artifacts.resourceManifestPath = resourcesManifestPath.string();
    artifacts.exportedResources.push_back("index/resources_manifest.json");

    std::sort(artifacts.exportedResources.begin(), artifacts.exportedResources.end());
    artifacts.exportedResources.erase(
        std::unique(artifacts.exportedResources.begin(), artifacts.exportedResources.end()),
        artifacts.exportedResources.end());

    return true;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
