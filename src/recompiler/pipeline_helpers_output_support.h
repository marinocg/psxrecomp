#pragma once

#include "pipeline_helpers.h"

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
                                std::string& outError);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
