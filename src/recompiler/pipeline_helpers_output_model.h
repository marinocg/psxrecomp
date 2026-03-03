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

struct EmbeddedScanSummary
{
    bool enabled = false;
    size_t containersScanned = 0;
    size_t hitsExtracted = 0;
};

struct DiscLayoutExtent
{
    u32 lba = 0;
    u32 sectors = 0;
    u64 fileByteOffset = 0;
};

struct DiscLayoutFile
{
    std::string path;
    u64 bytes = 0;
    std::vector<DiscLayoutExtent> extents;
};

struct DiscBlobSummary
{
    bool enabled = false;
    std::string mode = "auto";
    std::string format = "data_track_user_2048";
    u32 sectorSize = 2048;
    u32 lbaStart = 0;
    u32 lbaCount = 0;
    u64 blobBytes = 0;
    bool truncated = false;
    u64 maxBytes = 0;
    std::string blobPath = "disc/data_track.bin";
    std::string layoutPath = "disc/disc_layout.json";
    std::string hashesPath = "disc/disc_hashes.json";
    std::string blobSha1;
};

struct CatalogEntry
{
    std::string id;
    std::string sourceKind = "disc_file";
    std::string isoPath;
    std::string exportedPath;
    u64 size = 0;
    std::string sha1;
    std::string containerIsoPath;
    u64 containerOffset = 0;
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
std::string serializeDiscLayout(const iso::IsoParser& parser, const DiscBlobSummary& summary,
                                const std::vector<DiscLayoutFile>& files);
std::string serializeDiscHashes(const DiscBlobSummary& summary);
std::string serializeResourcesManifest(
    const std::string& inputPath, const std::string& timestamp, const std::string& pipelineVersion,
    const iso::IsoParser& parser, const std::vector<iso::IsoFileEntry>& entries,
    const FilesystemExportSummary& filesystemSummary, const DiscBlobSummary& discBlobSummary,
    const EmbeddedScanSummary& embeddedScanSummary, const std::vector<std::string>& extraWarnings);
std::string serializeRecompInputs(const std::string& bootIsoPath,
                                  const std::string& bootExportedPath,
                                  const std::string& systemCnfExportedPath,
                                  const std::vector<RecompInputExecutable>& executables);
std::string serializeCatalog(const std::vector<CatalogEntry>& entries);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
