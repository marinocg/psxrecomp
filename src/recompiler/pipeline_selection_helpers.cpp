#include "pipeline_selection_helpers.h"

#include "pipeline_helpers.h"

#include <algorithm>
#include <optional>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

PipelineResult buildPipelineError(const std::string& message,
                                  const std::vector<std::string>& warnings,
                                  const std::vector<PipelineDiagnostic>& diagnostics,
                                  const std::vector<ExeCandidateInfo>& candidates)
{
    PipelineResult result;
    result.success = false;
    result.errorMessage = message;
    result.warnings = warnings;
    result.diagnostics = diagnostics;
    result.exeCandidates = candidates;
    return result;
}

std::vector<std::string> deduplicatePathsCaseInsensitive(const std::vector<std::string>& paths)
{
    std::vector<std::string> normalized;
    std::vector<std::string> uniquePaths;
    for (const auto& path : paths)
    {
        std::string key = toLower(path);
        if (std::find(normalized.begin(), normalized.end(), key) == normalized.end())
        {
            normalized.push_back(std::move(key));
            uniquePaths.push_back(path);
        }
    }
    return uniquePaths;
}

bool exeCandidateLess(const ExeCandidateInfo& lhs, const ExeCandidateInfo& rhs)
{
    const std::string lhsKey = toLower(lhs.path);
    const std::string rhsKey = toLower(rhs.path);
    if (lhsKey != rhsKey)
    {
        return lhsKey < rhsKey;
    }
    if (lhs.loadAddress != rhs.loadAddress)
    {
        return lhs.loadAddress < rhs.loadAddress;
    }
    if (lhs.loadSize != rhs.loadSize)
    {
        return lhs.loadSize < rhs.loadSize;
    }
    if (lhs.entryPoint != rhs.entryPoint)
    {
        return lhs.entryPoint < rhs.entryPoint;
    }
    if (lhs.hash != rhs.hash)
    {
        return lhs.hash < rhs.hash;
    }
    if (lhs.valid != rhs.valid)
    {
        return lhs.valid;
    }
    return lhs.path < rhs.path;
}

std::optional<size_t> selectExeCandidateIndex(const std::vector<ExeCandidateInfo>& candidates,
                                              const std::string& bootPath,
                                              std::string& selectionReason)
{
    if (!bootPath.empty())
    {
        const std::string loweredBootPath = toLower(bootPath);
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            const auto& candidate = candidates[index];
            if (candidate.valid && toLower(candidate.path) == loweredBootPath)
            {
                selectionReason = "Selected SYSTEM.CNF BOOT candidate.";
                return index;
            }
        }
    }

    for (size_t index = 0; index < candidates.size(); ++index)
    {
        if (candidates[index].valid)
        {
            selectionReason = "Selected first valid candidate after sorting.";
            return index;
        }
    }

    return std::nullopt;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
