#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

struct WorkspaceExecutableInfo
{
    std::string isoPath;
    std::string exportedPath;
};

struct ResourceWorkspaceInfo
{
    std::filesystem::path workspaceRoot;
    std::filesystem::path recompInputsPath;
    std::filesystem::path discTreePath;
    std::filesystem::path discMetaPath;
    std::filesystem::path resourcesManifestPath;
    std::string bootIsoPath;
    std::string bootExportedPath;
    std::vector<WorkspaceExecutableInfo> executables;
    std::string discMetaInputPath;
    std::string discMetaVolumeLabel;
};

bool detectResourceWorkspaceRoot(const std::filesystem::path& inputPath,
                                 std::filesystem::path& outWorkspaceRoot);
bool loadResourceWorkspaceInfo(const std::filesystem::path& workspaceRoot,
                               ResourceWorkspaceInfo& outInfo, std::string& outError);
bool resolveWorkspaceExecutableHostPath(const ResourceWorkspaceInfo& workspaceInfo,
                                        const WorkspaceExecutableInfo& executableInfo,
                                        std::filesystem::path& outHostPath, std::string& outError);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
