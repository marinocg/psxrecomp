#include "pipeline_workspace_helpers.h"

#include "pipeline_helpers.h"
#include "pipeline_workspace_json.h"

#include <cctype>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{

bool isPathWithin(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    auto rootIt = root.begin();
    auto candidateIt = candidate.begin();
    for (; rootIt != root.end(); ++rootIt, ++candidateIt)
    {
        if (candidateIt == candidate.end() || *rootIt != *candidateIt)
        {
            return false;
        }
    }
    return true;
}

bool validateWorkspaceExportedPath(const std::string& rawPath, std::filesystem::path& outPath,
                                   std::string& outError)
{
    std::filesystem::path normalized = std::filesystem::path(rawPath).lexically_normal();
    if (normalized.empty())
    {
        outError = "Path is empty.";
        return false;
    }
    if (normalized.has_root_name() || normalized.has_root_directory() || normalized.is_absolute())
    {
        outError = "Path must be relative to the workspace root.";
        return false;
    }

    size_t componentCount = 0;
    std::string firstComponent;
    for (const auto& component : normalized)
    {
        const std::string token = component.string();
        if (token.empty())
        {
            continue;
        }
        if (token == "." || token == "..")
        {
            outError = "Path traversal segments are not allowed.";
            return false;
        }
        if (componentCount == 0)
        {
            firstComponent = token;
        }
        ++componentCount;
    }

    if (componentCount < 2)
    {
        outError = "Path must reference a file under fs/.";
        return false;
    }
    if (toLower(firstComponent) != "fs")
    {
        outError = "Path must be under fs/.";
        return false;
    }

    outPath = normalized;
    return true;
}

} // namespace

bool detectResourceWorkspaceRoot(const std::filesystem::path& inputPath,
                                 std::filesystem::path& outWorkspaceRoot)
{
    std::error_code error;
    if (!std::filesystem::is_directory(inputPath, error) || error)
    {
        return false;
    }

    const std::filesystem::path directRecompInputs = inputPath / "index" / "recomp_inputs.json";
    if (std::filesystem::is_regular_file(directRecompInputs, error) && !error)
    {
        outWorkspaceRoot = inputPath;
        return true;
    }

    error.clear();
    const std::filesystem::path nestedWorkspace = inputPath / "resources";
    const std::filesystem::path nestedRecompInputs =
        nestedWorkspace / "index" / "recomp_inputs.json";
    if (std::filesystem::is_regular_file(nestedRecompInputs, error) && !error)
    {
        outWorkspaceRoot = nestedWorkspace;
        return true;
    }

    return false;
}

bool loadResourceWorkspaceInfo(const std::filesystem::path& workspaceRoot,
                               ResourceWorkspaceInfo& outInfo, std::string& outError)
{
    outInfo = ResourceWorkspaceInfo{};
    outInfo.workspaceRoot = workspaceRoot;
    outInfo.recompInputsPath = workspaceRoot / "index" / "recomp_inputs.json";
    outInfo.discTreePath = workspaceRoot / "index" / "disc_tree.json";
    outInfo.discMetaPath = workspaceRoot / "index" / "disc_meta.json";
    outInfo.resourcesManifestPath = workspaceRoot / "index" / "resources_manifest.json";

    std::error_code error;
    if (!std::filesystem::is_regular_file(outInfo.recompInputsPath, error) || error)
    {
        outError = "Missing required workspace index file: " + outInfo.recompInputsPath.string();
        return false;
    }

    std::string recompInputsText;
    if (!readTextFile(outInfo.recompInputsPath, recompInputsText, outError))
    {
        return false;
    }

    JsonValue recompInputsRoot;
    JsonParser recompInputsParser(recompInputsText);
    if (!recompInputsParser.parse(recompInputsRoot, outError))
    {
        outError = "Failed to parse recomp inputs JSON: " + outError;
        return false;
    }
    if (recompInputsRoot.type != JsonValue::Type::Object)
    {
        outError = "Invalid recomp inputs JSON: expected top-level object.";
        return false;
    }

    const JsonValue* bootObject = expectObjectField(recompInputsRoot, "boot");
    if (bootObject)
    {
        outInfo.bootIsoPath = readStringField(*bootObject, "isoPath");
        outInfo.bootExportedPath = readStringField(*bootObject, "exportedPath");
        if (!outInfo.bootExportedPath.empty())
        {
            std::filesystem::path normalizedBootPath;
            std::string bootPathError;
            if (!validateWorkspaceExportedPath(outInfo.bootExportedPath, normalizedBootPath,
                                               bootPathError))
            {
                outError = "Invalid boot.exportedPath '" + outInfo.bootExportedPath +
                           "': " + bootPathError;
                return false;
            }
            outInfo.bootExportedPath = normalizedBootPath.generic_string();
        }
    }

    const JsonValue* executableArray = expectArrayField(recompInputsRoot, "executables");
    if (!executableArray)
    {
        outError = "Invalid recomp inputs JSON: missing executables array.";
        return false;
    }
    for (size_t index = 0; index < executableArray->arrayValue.size(); ++index)
    {
        const JsonValue& executableValue = executableArray->arrayValue[index];
        if (executableValue.type != JsonValue::Type::Object)
        {
            outError = "Invalid recomp inputs JSON: executable entry at index " +
                       std::to_string(index) + " is not an object.";
            return false;
        }

        WorkspaceExecutableInfo executableInfo;
        executableInfo.isoPath = readStringField(executableValue, "isoPath");
        executableInfo.exportedPath = readStringField(executableValue, "exportedPath");
        if (executableInfo.isoPath.empty())
        {
            outError = "Invalid recomp inputs JSON: executable entry at index " +
                       std::to_string(index) + " is missing isoPath.";
            return false;
        }
        if (executableInfo.exportedPath.empty())
        {
            executableInfo.exportedPath =
                (std::filesystem::path("fs") / std::filesystem::path(executableInfo.isoPath))
                    .generic_string();
        }

        std::filesystem::path normalizedExecutablePath;
        std::string executablePathError;
        if (!validateWorkspaceExportedPath(executableInfo.exportedPath, normalizedExecutablePath,
                                           executablePathError))
        {
            outError = "Invalid executables[" + std::to_string(index) + "].exportedPath '" +
                       executableInfo.exportedPath + "': " + executablePathError;
            return false;
        }
        executableInfo.exportedPath = normalizedExecutablePath.generic_string();
        outInfo.executables.push_back(std::move(executableInfo));
    }

    if (outInfo.executables.empty())
    {
        outError = "Invalid recomp inputs JSON: executables array is empty.";
        return false;
    }

    if (outInfo.bootIsoPath.empty() && !outInfo.bootExportedPath.empty())
    {
        const std::string bootExportedPathLower = toLower(outInfo.bootExportedPath);
        for (const auto& executable : outInfo.executables)
        {
            if (toLower(executable.exportedPath) == bootExportedPathLower)
            {
                outInfo.bootIsoPath = executable.isoPath;
                break;
            }
        }
    }
    if (outInfo.bootIsoPath.empty() && outInfo.executables.size() == 1)
    {
        outInfo.bootIsoPath = outInfo.executables.front().isoPath;
    }
    if (outInfo.bootExportedPath.empty() && !outInfo.bootIsoPath.empty())
    {
        outInfo.bootExportedPath =
            (std::filesystem::path("fs") / std::filesystem::path(outInfo.bootIsoPath))
                .generic_string();
    }

    error.clear();
    if (std::filesystem::is_regular_file(outInfo.discMetaPath, error) && !error)
    {
        std::string discMetaText;
        if (!readTextFile(outInfo.discMetaPath, discMetaText, outError))
        {
            return false;
        }

        JsonValue discMetaRoot;
        JsonParser discMetaParser(discMetaText);
        if (!discMetaParser.parse(discMetaRoot, outError))
        {
            outError = "Failed to parse disc meta JSON: " + outError;
            return false;
        }
        if (discMetaRoot.type == JsonValue::Type::Object)
        {
            outInfo.discMetaInputPath = readStringField(discMetaRoot, "inputPath");
            outInfo.discMetaVolumeLabel = readStringField(discMetaRoot, "volumeLabel");
        }
    }

    return true;
}

bool resolveWorkspaceExecutableHostPath(const ResourceWorkspaceInfo& workspaceInfo,
                                        const WorkspaceExecutableInfo& executableInfo,
                                        std::filesystem::path& outHostPath, std::string& outError)
{
    std::filesystem::path relativePath;
    if (!validateWorkspaceExportedPath(executableInfo.exportedPath, relativePath, outError))
    {
        return false;
    }

    std::error_code error;
    const std::filesystem::path canonicalWorkspaceRoot =
        std::filesystem::weakly_canonical(workspaceInfo.workspaceRoot, error);
    if (error)
    {
        outError = "Failed to canonicalize workspace root '" +
                   workspaceInfo.workspaceRoot.string() + "': " + error.message();
        return false;
    }

    const std::filesystem::path hostPath = canonicalWorkspaceRoot / relativePath;
    const std::filesystem::path canonicalHostPath =
        std::filesystem::weakly_canonical(hostPath, error);
    if (error)
    {
        outError = "Failed to canonicalize executable path '" + hostPath.string() +
                   "': " + error.message();
        return false;
    }

    if (!isPathWithin(canonicalWorkspaceRoot, canonicalHostPath))
    {
        outError = "Resolved executable path escapes the workspace root.";
        return false;
    }

    outHostPath = canonicalHostPath;
    return true;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
