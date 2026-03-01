#include "pipeline_helpers_output_support.h"

#include "pipeline_helpers_output_model.h"
#include "pipeline_selection_helpers.h"

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

    FilesystemExportSummary filesystemSummary;
    filesystemSummary.mode = fsModeToString(resourceExportOptions.fsMode);
    filesystemSummary.maxTotalBytes = resourceExportOptions.maxTotalBytes;
    filesystemSummary.maxSingleFileBytes = resourceExportOptions.maxSingleFileBytes;
    std::vector<std::string> resourceManifestWarnings;
    std::unordered_map<std::string, std::string> exportedPathByIsoPath;

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

        const std::string exportedPath =
            (std::filesystem::path("fs") / std::filesystem::path(isoRelativePath)).generic_string();
        artifacts.exportedResources.push_back(exportedPath);
        exportedPathByIsoPath.emplace(toLower(isoRelativePath), exportedPath);
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

    std::vector<std::string> executablePaths = parser.listExecutables();
    if (!bootExecutable.empty())
    {
        executablePaths.push_back(bootExecutable);
    }
    const std::vector<std::string> uniqueExecutablePaths =
        deduplicatePathsCaseInsensitive(executablePaths);

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
