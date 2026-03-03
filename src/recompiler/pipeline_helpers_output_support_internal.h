#pragma once

#include "pipeline_helpers_output_model.h"

#include <array>
#include <cstddef>
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

class Sha1Hasher
{
  public:
    static constexpr size_t kBlockSize = 64;
    static constexpr size_t kDigestSize = 20;

    Sha1Hasher();
    void update(const u8* data, size_t size);
    std::array<u8, kDigestSize> finalize();

  private:
    void processBlock(const u8* block);

    std::array<u32, 5> m_state{};
    std::array<u8, kBlockSize> m_buffer{};
    u64 m_totalBytes = 0;
    size_t m_bufferSize = 0;
};

std::string sha1DigestToHex(const std::array<u8, Sha1Hasher::kDigestSize>& digest);
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
bool exportDiscDataTrackArtifacts(const std::filesystem::path& resourcesRoot,
                                  iso::IsoParser& parser,
                                  const std::vector<iso::IsoFileEntry>& isoTreeEntries,
                                  const PipelineOptions::ResourceExportOptions& resourceOptions,
                                  std::vector<std::string>& warnings,
                                  std::vector<std::string>& manifestWarnings,
                                  PipelineArtifacts& artifacts, DiscBlobSummary& outSummary,
                                  std::string& outError);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
