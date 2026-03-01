#include "pipeline_helpers.h"

#include "pipeline_helpers_output_support.h"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

bool writeOutputArtifacts(
    PipelineResult& result, const std::filesystem::path& outputDir, const std::string& moduleName,
    const std::string& header, const std::string& source, const std::string& runnerSource,
    const std::string& buildFile, const std::string& activeDiscPath,
    const std::filesystem::path& inputFsPath, const std::string& resourceWorkspacePath,
    const PipelineOptions::ResourceExportOptions& resourceExportOptions,
    std::vector<std::string>& warnings, std::vector<PipelineDiagnostic>& diagnostics,
    const std::string& manifestTimestamp, const std::string& pipelineVersion, std::string& outError)
{
    std::error_code dirError;
    std::filesystem::create_directories(outputDir, dirError);
    if (dirError)
    {
        outError = "Failed to create output directory: " + outputDir.string();
        return false;
    }

    PipelineArtifacts artifacts;
    artifacts.moduleName = moduleName;
    artifacts.headerPath = (outputDir / (moduleName + ".h")).string();
    artifacts.sourcePath = (outputDir / (moduleName + ".cpp")).string();
    const std::string runnerPath = (outputDir / (moduleName + "_runner.cpp")).string();
    artifacts.buildPath = (outputDir / "CMakeLists.txt").string();
    artifacts.manifestPath = (outputDir / "manifest.json").string();
    artifacts.resourcesPath = (outputDir / "resources").string();
    artifacts.resourceRootPath = artifacts.resourcesPath;
    artifacts.resourceManifestPath = "";
    artifacts.runtimeIncludePath = (outputDir / "runtime" / "include").string();
    artifacts.runtimeSourcePath = (outputDir / "runtime" / "src").string();
    const std::string timestamp = buildTimestamp(manifestTimestamp);

    if (!writeFile(artifacts.headerPath, header, outError) ||
        !writeFile(artifacts.sourcePath, source, outError) ||
        !writeFile(runnerPath, runnerSource, outError) ||
        !writeFile(artifacts.buildPath, buildFile, outError))
    {
        return false;
    }

    std::filesystem::path repoRoot = repositoryRootFromSourcePath(std::filesystem::path(__FILE__));
    if (!copyDirectoryRecursive(repoRoot / "include" / "psxrecomp",
                                std::filesystem::path(artifacts.runtimeIncludePath) / "psxrecomp",
                                outError) ||
        !copyDirectoryRecursive(repoRoot / "src" / "runtime", artifacts.runtimeSourcePath,
                                outError))
    {
        return false;
    }

    if (!resourceWorkspacePath.empty())
    {
        const std::filesystem::path sourceWorkspace(resourceWorkspacePath);
        const std::filesystem::path destinationWorkspace(artifacts.resourcesPath);
        std::error_code resourceError;
        std::filesystem::remove_all(destinationWorkspace, resourceError);
        if (resourceError)
        {
            outError =
                "Failed to clear output resources directory: " + destinationWorkspace.string();
            return false;
        }
        if (!copyDirectoryRecursive(sourceWorkspace, destinationWorkspace, outError))
        {
            return false;
        }

        std::vector<std::string> copiedResources;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(destinationWorkspace))
        {
            resourceError.clear();
            if (!entry.is_regular_file(resourceError) || resourceError)
            {
                continue;
            }
            std::filesystem::path relativePath =
                std::filesystem::relative(entry.path(), destinationWorkspace, resourceError);
            if (resourceError)
            {
                outError = "Failed to relativize copied resource path: " + entry.path().string();
                return false;
            }
            copiedResources.push_back(relativePath.generic_string());
        }
        std::sort(copiedResources.begin(), copiedResources.end());
        artifacts.exportedResources = std::move(copiedResources);

        const std::filesystem::path resourceManifestPath =
            destinationWorkspace / "index" / "resources_manifest.json";
        resourceError.clear();
        if (std::filesystem::is_regular_file(resourceManifestPath, resourceError) && !resourceError)
        {
            artifacts.resourceManifestPath = resourceManifestPath.string();
        }
    }
    else if (isIsoLikePath(inputFsPath) &&
             !exportIsoResourceArtifacts(artifacts, activeDiscPath, resourceExportOptions, warnings,
                                         diagnostics, timestamp, pipelineVersion, outError))
    {
        return false;
    }

    result.success = true;
    result.artifacts = artifacts;
    result.warnings = warnings;
    result.diagnostics = diagnostics;

    const std::string manifest =
        serializeManifest(result, activeDiscPath, outputDir.string(), timestamp, pipelineVersion);
    if (!writeFile(artifacts.manifestPath, manifest, outError))
    {
        return false;
    }
    return true;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
