#include "pipeline_helpers_output_support.h"

#include "pipeline_helpers_output_model.h"
#include "pipeline_helpers_output_support_internal.h"
#include "pipeline_selection_helpers.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

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
