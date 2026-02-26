#pragma once

#include "psxrecomp/recompiler/pipeline.h"

#include <optional>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

PipelineResult buildPipelineError(const std::string& message,
                                  const std::vector<std::string>& warnings,
                                  const std::vector<PipelineDiagnostic>& diagnostics,
                                  const std::vector<ExeCandidateInfo>& candidates);
std::vector<std::string> deduplicatePathsCaseInsensitive(const std::vector<std::string>& paths);
bool exeCandidateLess(const ExeCandidateInfo& lhs, const ExeCandidateInfo& rhs);
std::optional<size_t> selectExeCandidateIndex(const std::vector<ExeCandidateInfo>& candidates,
                                              const std::string& bootPath,
                                              std::string& selectionReason);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
