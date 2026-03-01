#pragma once

#include "pipeline_helpers_output_model.h"

#include <filesystem>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

struct EmbeddedHit
{
    std::string containerIsoPath;
    u64 offset = 0;
    u64 size = 0;
    std::string sha1;
    std::string outputPath;
};

bool ensureSha1SelfTest(std::string& outError);
bool computeSha1AndPrefix(const std::filesystem::path& path, std::string& outSha1, u64& outSize,
                          std::vector<u8>& outPrefix, std::string& outError);
std::vector<std::string> detectCatalogTypes(const std::string& isoPath, const std::vector<u8>& data,
                                            u64 fileSize, bool hasRawXaMetadata,
                                            bool forceExecutableType);
bool scanEmbeddedTimResources(iso::IsoParser& parser, const std::vector<iso::IsoFileEntry>& entries,
                              const std::filesystem::path& resourcesRoot,
                              const PipelineOptions::ResourceExportOptions& options,
                              std::vector<std::string>& warnings,
                              std::vector<std::string>& resourceManifestWarnings,
                              EmbeddedScanSummary& summary, std::vector<EmbeddedHit>& outHits,
                              std::string& outError);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
