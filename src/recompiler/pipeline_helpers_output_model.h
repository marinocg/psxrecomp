#pragma once

#include "pipeline_helpers.h"

#include "psxrecomp/iso/iso_parser.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

struct FilesystemExportPlan
{
    std::vector<std::string> paths;
    size_t skippedDueToLimits = 0;
    std::vector<std::string> alwaysIncludedRules;
};

struct FilesystemExportSummary
{
    std::string mode;
    u64 maxTotalBytes = 0;
    u64 maxSingleFileBytes = 0;
    size_t filesExported = 0;
    u64 bytesExported = 0;
    size_t skippedDueToLimits = 0;
    std::vector<std::string> alwaysIncludedRules;
};

struct RecompInputExecutable
{
    std::string isoPath;
    std::string exportedPath;
    u32 loadAddress = 0;
    u32 loadSize = 0;
    u32 entryPoint = 0;
    u32 gp = 0;
    u32 bssAddress = 0;
    u32 bssSize = 0;
    u32 stackAddress = 0;
    u32 stackSize = 0;
};

struct CatalogEntry
{
    std::string id;
    std::string isoPath;
    std::string exportedPath;
    u64 size = 0;
    std::string sha1;
    std::vector<iso::IsoFileExtent> extents;
    std::vector<std::string> detectedTypes;
};

std::string fsModeToString(PipelineOptions::ResourceExportOptions::FsMode mode);
FilesystemExportPlan
buildFilesystemExportPlan(const std::vector<iso::IsoFileEntry>& entries,
                          const std::string& bootExecutable,
                          const PipelineOptions::ResourceExportOptions& options);

std::string serializeDiscTree(const std::vector<iso::IsoFileEntry>& entries);
std::string serializeDiscMeta(const std::string& inputPath, const std::string& bootExecutable,
                              const iso::IsoParser& parser,
                              const std::vector<iso::IsoFileEntry>& entries);
std::string serializeResourcesManifest(const std::string& inputPath, const std::string& timestamp,
                                       const std::string& pipelineVersion,
                                       const iso::IsoParser& parser,
                                       const std::vector<iso::IsoFileEntry>& entries,
                                       const FilesystemExportSummary& filesystemSummary,
                                       const std::vector<std::string>& extraWarnings);
std::string serializeRecompInputs(const std::string& bootIsoPath,
                                  const std::string& bootExportedPath,
                                  const std::string& systemCnfExportedPath,
                                  const std::vector<RecompInputExecutable>& executables);
std::string serializeCatalog(const std::vector<CatalogEntry>& entries);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
