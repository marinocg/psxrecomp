#include "pipeline_helpers_output_support.h"

#include "pipeline_helpers_output_model.h"

#include <algorithm>
#include <filesystem>
#include <unordered_map>

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
        outError = "Failed to create resources directory: " + artifacts.resourcesPath;
        return false;
    }
    std::filesystem::remove_all(resourcesFs, dirError);
    if (dirError)
    {
        outError = "Failed to clear filesystem resources directory: " + resourcesFs.string();
        return false;
    }
    std::filesystem::create_directories(resourcesFs, dirError);
    if (dirError)
    {
        outError = "Failed to create filesystem resources directory: " + resourcesFs.string();
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

    FilesystemExportSummary filesystemSummary;
    filesystemSummary.mode = fsModeToString(resourceExportOptions.fsMode);
    filesystemSummary.maxTotalBytes = resourceExportOptions.maxTotalBytes;
    filesystemSummary.maxSingleFileBytes = resourceExportOptions.maxSingleFileBytes;
    std::vector<std::string> resourceManifestWarnings;

    std::unordered_map<std::string, u32> fileSizeByPath;
    for (const auto& entry : isoTreeEntries)
    {
        if (entry.isDirectory)
        {
            continue;
        }
        fileSizeByPath.emplace(entry.path, entry.size);
    }

    const FilesystemExportPlan exportPlan =
        buildFilesystemExportPlan(isoTreeEntries, bootExecutable, resourceExportOptions);
    filesystemSummary.skippedDueToLimits = exportPlan.skippedDueToLimits;
    filesystemSummary.alwaysIncludedRules = exportPlan.alwaysIncludedRules;

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

        artifacts.exportedResources.push_back(
            (std::filesystem::path("fs") / std::filesystem::path(isoRelativePath))
                .generic_string());
        ++filesystemSummary.filesExported;
        const auto sizeIt = fileSizeByPath.find(isoRelativePath);
        if (sizeIt != fileSizeByPath.end())
        {
            filesystemSummary.bytesExported += static_cast<u64>(sizeIt->second);
        }
    }

    if (filesystemSummary.filesExported == 0)
    {
        const std::string warning =
            "Filesystem export produced no files. Check export policy and ISO contents.";
        warnings.push_back(warning);
        resourceManifestWarnings.push_back(warning);
    }

    const auto resourcesManifestPath = resourcesIndex / "resources_manifest.json";
    if (!writeFile(resourcesManifestPath,
                   serializeResourcesManifest(activeDiscPath, timestamp, pipelineVersion, parser,
                                              isoTreeEntries, filesystemSummary,
                                              resourceManifestWarnings),
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
