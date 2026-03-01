#include "pipeline_helpers_output_model.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{

std::string toUpperCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string normalizeIsoComparablePath(const std::string& path)
{
    std::vector<std::string> parts;
    std::string current;
    for (char ch : path)
    {
        if (ch == '/' || ch == '\\')
        {
            if (!current.empty())
            {
                auto semicolon = current.find(';');
                if (semicolon != std::string::npos)
                {
                    current.erase(semicolon);
                }
                current = toUpperCopy(current);
                if (!current.empty() && current != "." && current != "..")
                {
                    parts.push_back(current);
                }
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }

    if (!current.empty())
    {
        auto semicolon = current.find(';');
        if (semicolon != std::string::npos)
        {
            current.erase(semicolon);
        }
        current = toUpperCopy(current);
        if (!current.empty() && current != "." && current != "..")
        {
            parts.push_back(current);
        }
    }

    std::ostringstream normalized;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        normalized << parts[i];
        if (i + 1 < parts.size())
        {
            normalized << "/";
        }
    }
    return normalized.str();
}

std::string normalizePrefix(const std::string& prefix)
{
    std::string normalized = normalizeIsoComparablePath(prefix);
    while (!normalized.empty() && normalized.back() == '/')
    {
        normalized.pop_back();
    }
    return normalized;
}

bool pathHasPrefix(const std::string& normalizedPath, const std::string& normalizedPrefix)
{
    if (normalizedPrefix.empty())
    {
        return false;
    }
    if (normalizedPath == normalizedPrefix)
    {
        return true;
    }
    return normalizedPath.size() > normalizedPrefix.size() &&
           normalizedPath.compare(0, normalizedPrefix.size(), normalizedPrefix) == 0 &&
           normalizedPath[normalizedPrefix.size()] == '/';
}

bool passesPrefixFilters(const std::string& path,
                         const PipelineOptions::ResourceExportOptions& options)
{
    const std::string normalizedPath = normalizeIsoComparablePath(path);
    for (const auto& denyPrefixRaw : options.denyPrefixes)
    {
        const std::string denyPrefix = normalizePrefix(denyPrefixRaw);
        if (pathHasPrefix(normalizedPath, denyPrefix))
        {
            return false;
        }
    }

    if (options.allowPrefixes.empty())
    {
        return true;
    }

    for (const auto& allowPrefixRaw : options.allowPrefixes)
    {
        const std::string allowPrefix = normalizePrefix(allowPrefixRaw);
        if (pathHasPrefix(normalizedPath, allowPrefix))
        {
            return true;
        }
    }
    return false;
}

bool isExePath(const std::string& path)
{
    const std::string normalizedPath = normalizeIsoComparablePath(path);
    if (normalizedPath.size() < 4)
    {
        return false;
    }
    return normalizedPath.compare(normalizedPath.size() - 4, 4, ".EXE") == 0;
}

} // namespace

std::string fsModeToString(PipelineOptions::ResourceExportOptions::FsMode mode)
{
    switch (mode)
    {
    case PipelineOptions::ResourceExportOptions::FsMode::Minimal:
        return "minimal";
    case PipelineOptions::ResourceExportOptions::FsMode::Smart:
        return "smart";
    case PipelineOptions::ResourceExportOptions::FsMode::Full:
    default:
        return "full";
    }
}

FilesystemExportPlan
buildFilesystemExportPlan(const std::vector<iso::IsoFileEntry>& entries,
                          const std::string& bootExecutable,
                          const PipelineOptions::ResourceExportOptions& options)
{
    FilesystemExportPlan plan;

    std::vector<const iso::IsoFileEntry*> files;
    files.reserve(entries.size());
    std::unordered_map<std::string, const iso::IsoFileEntry*> fileByPath;

    for (const auto& entry : entries)
    {
        if (entry.isDirectory)
        {
            continue;
        }
        files.push_back(&entry);
        fileByPath.emplace(normalizeIsoComparablePath(entry.path), &entry);
    }

    std::sort(files.begin(), files.end(),
              [](const iso::IsoFileEntry* lhs, const iso::IsoFileEntry* rhs)
              { return lhs->path < rhs->path; });

    std::unordered_set<std::string> selected;
    auto selectPath = [&](const std::string& rawPath)
    {
        const std::string normalized = normalizeIsoComparablePath(rawPath);
        if (normalized.empty())
        {
            return;
        }
        const auto it = fileByPath.find(normalized);
        if (it == fileByPath.end())
        {
            return;
        }
        selected.insert(it->second->path);
    };

    if (options.alwaysExportSystemCnf)
    {
        plan.alwaysIncludedRules.push_back("systemCnf");
        selectPath("SYSTEM.CNF");
    }
    if (options.alwaysExportBootExe)
    {
        plan.alwaysIncludedRules.push_back("bootExecutable");
        selectPath(bootExecutable);
    }
    if (options.alwaysExportAllExe)
    {
        plan.alwaysIncludedRules.push_back("allExe");
        for (const auto* file : files)
        {
            if (isExePath(file->path))
            {
                selected.insert(file->path);
            }
        }
    }

    std::vector<std::string> alwaysPaths(selected.begin(), selected.end());
    std::sort(alwaysPaths.begin(), alwaysPaths.end());
    plan.paths = alwaysPaths;

    if (options.fsMode == PipelineOptions::ResourceExportOptions::FsMode::Minimal)
    {
        return plan;
    }

    if (options.fsMode == PipelineOptions::ResourceExportOptions::FsMode::Full)
    {
        for (const auto* file : files)
        {
            if (selected.find(file->path) != selected.end())
            {
                continue;
            }
            if (!passesPrefixFilters(file->path, options))
            {
                continue;
            }
            plan.paths.push_back(file->path);
        }
        std::sort(plan.paths.begin(), plan.paths.end());
        return plan;
    }

    struct SmartCandidate
    {
        const iso::IsoFileEntry* entry = nullptr;
    };

    std::vector<SmartCandidate> smartCandidates;
    smartCandidates.reserve(files.size());

    u64 accumulatedAlwaysBytes = 0;
    for (const auto& selectedPath : selected)
    {
        const auto it = fileByPath.find(normalizeIsoComparablePath(selectedPath));
        if (it == fileByPath.end())
        {
            continue;
        }
        accumulatedAlwaysBytes += static_cast<u64>(it->second->size);
    }

    for (const auto* file : files)
    {
        if (selected.find(file->path) != selected.end())
        {
            continue;
        }
        if (!passesPrefixFilters(file->path, options))
        {
            continue;
        }
        if (static_cast<u64>(file->size) > options.maxSingleFileBytes)
        {
            ++plan.skippedDueToLimits;
            continue;
        }
        smartCandidates.push_back({file});
    }

    std::sort(smartCandidates.begin(), smartCandidates.end(),
              [](const SmartCandidate& lhs, const SmartCandidate& rhs)
              {
                  if (lhs.entry->size != rhs.entry->size)
                  {
                      return lhs.entry->size < rhs.entry->size;
                  }
                  return lhs.entry->path < rhs.entry->path;
              });

    u64 smartBytes = accumulatedAlwaysBytes;
    for (const auto& candidate : smartCandidates)
    {
        const u64 candidateBytes = static_cast<u64>(candidate.entry->size);
        if (candidateBytes > options.maxTotalBytes ||
            smartBytes > options.maxTotalBytes - candidateBytes)
        {
            ++plan.skippedDueToLimits;
            continue;
        }
        smartBytes += candidateBytes;
        plan.paths.push_back(candidate.entry->path);
    }

    return plan;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
