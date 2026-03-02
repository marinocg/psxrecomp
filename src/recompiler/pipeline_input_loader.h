#pragma once

#include "pipeline_workspace_helpers.h"
#include "psxrecomp/iso/psx_exe_loader.h"
#include "psxrecomp/recompiler/pipeline.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

bool loadExecutableImageFromInput(const std::string& activeDiscPath,
                                  const std::filesystem::path& inputFsPath, PipelineResult& result,
                                  std::optional<ResourceWorkspaceInfo>& outWorkspaceInfo,
                                  std::vector<std::string>& warnings,
                                  std::vector<PipelineDiagnostic>& diagnostics,
                                  iso::PsxExeImage& outExeImage, std::string& outError);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
