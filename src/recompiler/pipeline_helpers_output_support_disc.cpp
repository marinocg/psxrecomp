#include "pipeline_helpers_output_support_internal.h"

#include "pipeline_selection_helpers.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{

constexpr u32 kDiscUserSectorSize = 2048;
constexpr u32 kDiscRawSectorSize = 2352;

u32 bytesToSectors(u64 byteCount, u32 sectorSize)
{
    if (sectorSize == 0 || byteCount == 0)
    {
        return 0;
    }
    return static_cast<u32>((byteCount + static_cast<u64>(sectorSize) - 1ULL) /
                            static_cast<u64>(sectorSize));
}

std::string discBlobModeToString(PipelineOptions::ResourceExportOptions::DiscBlobMode mode)
{
    switch (mode)
    {
    case PipelineOptions::ResourceExportOptions::DiscBlobMode::Force2048:
        return "force2048";
    case PipelineOptions::ResourceExportOptions::DiscBlobMode::Force2352:
        return "force2352";
    case PipelineOptions::ResourceExportOptions::DiscBlobMode::Disabled:
        return "disabled";
    case PipelineOptions::ResourceExportOptions::DiscBlobMode::Auto:
    default:
        return "auto";
    }
}

void clearDiscBlobArtifactSummaryFields(DiscBlobSummary& summary)
{
    summary.format.clear();
    summary.sectorSize = 0;
    summary.lbaStart = 0;
    summary.lbaCount = 0;
    summary.blobBytes = 0;
    summary.truncated = false;
    summary.blobPath.clear();
    summary.layoutPath.clear();
    summary.hashesPath.clear();
    summary.blobSha1.clear();
}

bool buildDiscLayoutFiles(const std::vector<iso::IsoFileEntry>& entries, u32 logicalBlockSize,
                          u32 discLbaBias, std::vector<DiscLayoutFile>& outFiles,
                          std::string& outError)
{
    outFiles.clear();
    outError.clear();

    std::vector<DiscLayoutFile> files;
    files.reserve(entries.size());

    for (const auto& entry : entries)
    {
        if (entry.isDirectory)
        {
            continue;
        }

        DiscLayoutFile file;
        file.path = entry.path;
        file.bytes = static_cast<u64>(entry.size);

        std::vector<iso::IsoFileExtent> sortedExtents = entry.extents;
        std::sort(sortedExtents.begin(), sortedExtents.end(),
                  [](const iso::IsoFileExtent& lhs, const iso::IsoFileExtent& rhs)
                  {
                      if (lhs.lba != rhs.lba)
                      {
                          return lhs.lba < rhs.lba;
                      }
                      if (lhs.size != rhs.size)
                      {
                          return lhs.size < rhs.size;
                      }
                      return lhs.continues && !rhs.continues;
                  });

        u64 fileByteOffset = 0;
        file.extents.reserve(sortedExtents.size());
        for (const auto& extent : sortedExtents)
        {
            const u64 discRelativeLba =
                static_cast<u64>(discLbaBias) + static_cast<u64>(extent.lba);
            if (discRelativeLba > static_cast<u64>(std::numeric_limits<u32>::max()))
            {
                outError = "Disc layout export failed: extent LBA exceeds 32-bit range.";
                return false;
            }

            DiscLayoutExtent layoutExtent;
            layoutExtent.lba = static_cast<u32>(discRelativeLba);
            layoutExtent.sectors = bytesToSectors(static_cast<u64>(extent.size), logicalBlockSize);
            layoutExtent.fileByteOffset = fileByteOffset;
            file.extents.push_back(layoutExtent);
            fileByteOffset += static_cast<u64>(extent.size);
        }

        files.push_back(std::move(file));
    }

    std::sort(files.begin(), files.end(),
              [](const DiscLayoutFile& lhs, const DiscLayoutFile& rhs)
              {
                  const std::string lhsKey = toLower(lhs.path);
                  const std::string rhsKey = toLower(rhs.path);
                  if (lhsKey != rhsKey)
                  {
                      return lhsKey < rhsKey;
                  }
                  return lhs.path < rhs.path;
              });
    outFiles = std::move(files);
    return true;
}

struct DiscLbaRange
{
    u32 start = 0;
    u32 count = 0;
};

bool determineDataTrackLbaRange(const iso::IsoParser& parser, DiscLbaRange& outRange,
                                std::string& outError)
{
    outRange = DiscLbaRange{};
    outError.clear();

    const auto& tracks = parser.getTracks();
    if (tracks.empty())
    {
        const u32 volumeCount = parser.getVolumeSpaceSize();
        if (volumeCount == 0)
        {
            outError = "Disc blob export failed: volume space size is zero.";
            return false;
        }
        outRange.start = 0;
        outRange.count = volumeCount;
        return true;
    }

    const auto dataTrack = parser.getDataTrack();
    if (!dataTrack.has_value())
    {
        outError = "Disc blob export failed: no data track found.";
        return false;
    }

    outRange.start = dataTrack->startLba;
    u32 nextTrackStart = std::numeric_limits<u32>::max();
    for (const auto& track : tracks)
    {
        if (track.startLba > outRange.start && track.startLba < nextTrackStart)
        {
            nextTrackStart = track.startLba;
        }
    }
    if (nextTrackStart != std::numeric_limits<u32>::max() && nextTrackStart > outRange.start)
    {
        outRange.count = nextTrackStart - outRange.start;
        return true;
    }

    const u32 volumeCount = parser.getVolumeSpaceSize();
    if (volumeCount != 0)
    {
        outRange.count = volumeCount;
        return true;
    }

    const u32 totalSectors = parser.getTotalSectors();
    if (totalSectors > outRange.start)
    {
        outRange.count = totalSectors - outRange.start;
        return true;
    }

    outError = "Disc blob export failed: unable to determine data-track LBA range.";
    return false;
}

bool readBlobSector(iso::IsoParser& parser, u32 relativeLba, u32 sectorSize, std::vector<u8>& out,
                    std::string& outError)
{
    out.clear();
    outError.clear();

    if (sectorSize == kDiscRawSectorSize)
    {
        if (!parser.readSectorRaw2352(relativeLba, out))
        {
            outError = parser.getLastError();
            return false;
        }
    }
    else if (sectorSize == kDiscUserSectorSize)
    {
        if (!parser.readSectorUser2048(relativeLba, out))
        {
            outError = parser.getLastError();
            return false;
        }
    }
    else
    {
        outError = "Unsupported disc blob sector size.";
        return false;
    }

    if (out.size() != sectorSize)
    {
        outError = "Unexpected sector size returned by parser.";
        out.clear();
        return false;
    }
    return true;
}

} // namespace

bool exportDiscDataTrackArtifacts(const std::filesystem::path& resourcesRoot,
                                  iso::IsoParser& parser,
                                  const std::vector<iso::IsoFileEntry>& isoTreeEntries,
                                  const PipelineOptions::ResourceExportOptions& resourceOptions,
                                  std::vector<std::string>& warnings,
                                  std::vector<std::string>& manifestWarnings,
                                  PipelineArtifacts& artifacts, DiscBlobSummary& outSummary,
                                  std::string& outError)
{
    outSummary = DiscBlobSummary{};
    outSummary.enabled = resourceOptions.discBlob.enabled;
    outSummary.mode = discBlobModeToString(resourceOptions.discBlob.mode);
    outSummary.maxBytes = resourceOptions.discBlob.maxBytes;

    std::error_code dirError;
    const std::filesystem::path discRoot = resourcesRoot / "disc";
    std::filesystem::remove_all(discRoot, dirError);
    if (dirError)
    {
        outError = "Failed to clear disc resources directory: " + discRoot.string() + ": " +
                   dirError.message();
        return false;
    }

    if (resourceOptions.discBlob.mode ==
        PipelineOptions::ResourceExportOptions::DiscBlobMode::Disabled)
    {
        outSummary.enabled = false;
        clearDiscBlobArtifactSummaryFields(outSummary);
        return true;
    }

    if (!resourceOptions.discBlob.enabled)
    {
        clearDiscBlobArtifactSummaryFields(outSummary);
        return true;
    }

    std::filesystem::create_directories(discRoot, dirError);
    if (dirError)
    {
        outError = "Failed to create disc resources directory: " + discRoot.string() + ": " +
                   dirError.message();
        return false;
    }

    outSummary.sectorSize = kDiscUserSectorSize;
    if (resourceOptions.discBlob.mode ==
        PipelineOptions::ResourceExportOptions::DiscBlobMode::Force2352)
    {
        if (!parser.canReadRaw2352())
        {
            outError = "Disc blob export failed: force2352 requested but raw 2352 sectors are "
                       "unavailable.";
            return false;
        }
        outSummary.sectorSize = kDiscRawSectorSize;
    }
    else if (resourceOptions.discBlob.mode ==
             PipelineOptions::ResourceExportOptions::DiscBlobMode::Force2048)
    {
        if (!parser.canReadUser2048())
        {
            outError = "Disc blob export failed: force2048 requested but 2048 user sectors are "
                       "unavailable.";
            return false;
        }
        outSummary.sectorSize = kDiscUserSectorSize;
    }
    else
    {
        outSummary.sectorSize = parser.canReadRaw2352() ? kDiscRawSectorSize : kDiscUserSectorSize;
    }

    if (outSummary.sectorSize == kDiscRawSectorSize)
    {
        outSummary.format = "data_track_raw_2352";
    }
    else
    {
        outSummary.format = "data_track_user_2048";
    }

    DiscLbaRange lbaRange{};
    if (!determineDataTrackLbaRange(parser, lbaRange, outError))
    {
        return false;
    }
    outSummary.lbaStart = lbaRange.start;

    u32 exportSectorCount = lbaRange.count;
    if (resourceOptions.discBlob.maxBytes > 0)
    {
        const u64 capSectors64 =
            resourceOptions.discBlob.maxBytes / static_cast<u64>(outSummary.sectorSize);
        if (capSectors64 == 0)
        {
            outError = "Disc blob export failed: maxBytes is smaller than one output sector.";
            return false;
        }
        const u32 capSectors = static_cast<u32>(
            std::min<u64>(capSectors64, static_cast<u64>(std::numeric_limits<u32>::max())));
        if (capSectors < exportSectorCount)
        {
            exportSectorCount = capSectors;
            outSummary.truncated = true;
            const std::string warning =
                "Disc blob export was truncated by maxBytes cap (lbaCount=" +
                std::to_string(exportSectorCount) + " of " + std::to_string(lbaRange.count) + ").";
            warnings.push_back(warning);
            manifestWarnings.push_back(warning);
        }
    }
    outSummary.lbaCount = exportSectorCount;

    const std::filesystem::path blobHostPath = discRoot / "data_track.bin";
    std::ofstream blobStream(blobHostPath, std::ios::binary | std::ios::trunc);
    if (!blobStream)
    {
        outError = "Failed to create disc blob file: " + blobHostPath.string();
        return false;
    }

    Sha1Hasher hasher;
    std::vector<u8> sectorData;
    sectorData.reserve(outSummary.sectorSize);
    std::string readError;

    for (u32 offset = 0; offset < exportSectorCount; ++offset)
    {
        const u32 absoluteLba = outSummary.lbaStart + offset;
        const u32 relativeLba = absoluteLba - lbaRange.start;
        if (!readBlobSector(parser, relativeLba, outSummary.sectorSize, sectorData, readError))
        {
            outError =
                "Disc blob export failed at LBA " + std::to_string(absoluteLba) + ": " + readError;
            return false;
        }

        blobStream.write(reinterpret_cast<const char*>(sectorData.data()),
                         static_cast<std::streamsize>(sectorData.size()));
        if (!blobStream.good())
        {
            outError =
                "Disc blob export failed while writing LBA " + std::to_string(absoluteLba) + ".";
            return false;
        }
        hasher.update(sectorData.data(), sectorData.size());
        outSummary.blobBytes += static_cast<u64>(sectorData.size());
    }

    blobStream.flush();
    if (!blobStream.good())
    {
        outError = "Disc blob export failed while flushing output file.";
        return false;
    }

    outSummary.blobSha1 = sha1DigestToHex(hasher.finalize());

    std::vector<DiscLayoutFile> discLayoutFiles;
    if (!buildDiscLayoutFiles(isoTreeEntries, parser.getLogicalBlockSize(), outSummary.lbaStart,
                              discLayoutFiles, outError))
    {
        return false;
    }
    const std::filesystem::path layoutHostPath = discRoot / "disc_layout.json";
    const std::filesystem::path hashesHostPath = discRoot / "disc_hashes.json";
    if (!writeFile(layoutHostPath, serializeDiscLayout(parser, outSummary, discLayoutFiles),
                   outError))
    {
        return false;
    }
    if (!writeFile(hashesHostPath, serializeDiscHashes(outSummary), outError))
    {
        return false;
    }

    const std::string blobResourcePath =
        (std::filesystem::path("disc") / "data_track.bin").generic_string();
    const std::string layoutResourcePath =
        (std::filesystem::path("disc") / "disc_layout.json").generic_string();
    const std::string hashesResourcePath =
        (std::filesystem::path("disc") / "disc_hashes.json").generic_string();
    artifacts.exportedResources.push_back(blobResourcePath);
    artifacts.exportedResources.push_back(layoutResourcePath);
    artifacts.exportedResources.push_back(hashesResourcePath);
    outSummary.blobPath = blobResourcePath;
    outSummary.layoutPath = layoutResourcePath;
    outSummary.hashesPath = hashesResourcePath;

    return true;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
