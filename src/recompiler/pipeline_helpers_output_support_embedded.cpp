#include "pipeline_helpers_output_support_internal.h"

#include "pipeline_selection_helpers.h"

#include "psxrecomp/iso/psx_exe_loader.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{

constexpr u64 kEmbeddedScanMaxContainers = 32;
constexpr u64 kEmbeddedScanMinContainerBytes = 256ULL * 1024ULL;
constexpr u64 kEmbeddedScanMaxBytesPerContainer = 128ULL * 1024ULL * 1024ULL;
constexpr u64 kEmbeddedScanMaxHitsPerContainer = 500;
constexpr size_t kEmbeddedScanChunkBytes = 1024 * 1024;
constexpr size_t kEmbeddedScanOverlapBytes = 3;

u32 readLe32(const std::vector<u8>& data, size_t offset)
{
    return static_cast<u32>(data[offset]) | (static_cast<u32>(data[offset + 1]) << 8) |
           (static_cast<u32>(data[offset + 2]) << 16) | (static_cast<u32>(data[offset + 3]) << 24);
}

u16 readLe16(const std::vector<u8>& data, size_t offset)
{
    return static_cast<u16>(data[offset]) | (static_cast<u16>(data[offset + 1]) << 8);
}

bool hasExtension(const std::string& path, const std::string& extensionLower)
{
    std::filesystem::path fsPath(path);
    std::string value = toLower(fsPath.extension().string());
    return value == extensionLower;
}

bool isLikelyContainerPath(const std::string& path)
{
    const std::string extension = toLower(std::filesystem::path(path).extension().string());
    if (extension.empty())
    {
        return true;
    }

    if (extension == ".exe" || extension == ".tim" || extension == ".str" || extension == ".xa" ||
        extension == ".cnf")
    {
        return false;
    }

    return extension == ".bin" || extension == ".dat" || extension == ".img" ||
           extension == ".pak" || extension == ".arc" || extension == ".res" || extension == ".wad";
}

std::string buildContainerId(const std::string& containerIsoPath)
{
    const std::string key = toLower(containerIsoPath);
    const std::vector<u8> keyBytes(key.begin(), key.end());
    return formatHex(fnv1a64(keyBytes), 16);
}

std::string embeddedBlobBaseName(u64 offset, u64 size)
{
    return std::to_string(offset) + "_" + std::to_string(size);
}

bool readIsoRange(iso::IsoParser& parser, const std::string& isoPath, u64 offset, size_t size,
                  std::vector<u8>& outData, std::string& outError)
{
    return parser.readRangeFromIsoFile(isoPath, offset, size, outData, &outError);
}

bool readIsoTimBlockHeader(iso::IsoParser& parser, const std::string& isoPath, u64 offset,
                           u32& outBlockSize, u16& outWidth, u16& outHeight, std::string& outError)
{
    std::vector<u8> bytes;
    if (!readIsoRange(parser, isoPath, offset, 12, bytes, outError))
    {
        return false;
    }
    outBlockSize = readLe32(bytes, 0);
    outWidth = readLe16(bytes, 8);
    outHeight = readLe16(bytes, 10);
    return true;
}

bool parseTimBlobAtOffset(iso::IsoParser& parser, const std::string& containerIsoPath, u64 offset,
                          u64 scanLimit, u64 containerSize, u64& outSize, std::string& outError)
{
    outSize = 0;
    if (offset + 8 > scanLimit || offset + 8 > containerSize)
    {
        return false;
    }

    std::vector<u8> header;
    if (!readIsoRange(parser, containerIsoPath, offset, 8, header, outError))
    {
        return false;
    }

    if (readLe32(header, 0) != 0x00000010U)
    {
        return false;
    }

    const u32 flags = readLe32(header, 4);
    if ((flags & ~0x0000000BU) != 0)
    {
        return false;
    }

    u64 cursor = offset + 8;

    auto parseBlock = [&](u64 blockOffset, u32& outBlockSize) -> bool
    {
        u16 width = 0;
        u16 height = 0;
        if (!readIsoTimBlockHeader(parser, containerIsoPath, blockOffset, outBlockSize, width,
                                   height, outError))
        {
            return false;
        }
        if (outBlockSize < 12 || width == 0 || height == 0)
        {
            return false;
        }
        if (blockOffset + static_cast<u64>(outBlockSize) > containerSize ||
            blockOffset + static_cast<u64>(outBlockSize) > scanLimit)
        {
            return false;
        }
        return true;
    };

    if ((flags & 0x08U) != 0)
    {
        u32 clutBlockSize = 0;
        if (!parseBlock(cursor, clutBlockSize))
        {
            return false;
        }
        cursor += clutBlockSize;
    }

    u32 imageBlockSize = 0;
    if (!parseBlock(cursor, imageBlockSize))
    {
        return false;
    }
    cursor += imageBlockSize;

    outSize = cursor - offset;
    return outSize != 0;
}

bool carveRangeToFileAndHash(iso::IsoParser& parser, const std::string& isoPath, u64 offset,
                             u64 size, const std::filesystem::path& destination,
                             std::string& outSha1, std::string& outError)
{
    std::error_code fsError;
    std::filesystem::create_directories(destination.parent_path(), fsError);
    if (fsError)
    {
        outError =
            "Failed to create embedded output directory: " + destination.parent_path().string() +
            ": " + fsError.message();
        return false;
    }

    std::ofstream out(destination, std::ios::binary);
    if (!out)
    {
        outError = "Failed to open embedded output file: " + destination.string();
        return false;
    }

    u64 remaining = size;
    u64 cursor = offset;
    while (remaining > 0)
    {
        const size_t readSize = static_cast<size_t>(
            std::min<u64>(remaining, static_cast<u64>(kEmbeddedScanChunkBytes)));
        std::vector<u8> chunk;
        if (!parser.readRangeFromIsoFile(isoPath, cursor, readSize, chunk, &outError))
        {
            return false;
        }
        if (chunk.size() != readSize)
        {
            outError = "Embedded carve read returned unexpected size.";
            return false;
        }

        out.write(reinterpret_cast<const char*>(chunk.data()),
                  static_cast<std::streamsize>(chunk.size()));
        if (!out.good())
        {
            outError = "Failed to write embedded output file: " + destination.string();
            return false;
        }

        cursor += static_cast<u64>(chunk.size());
        remaining -= static_cast<u64>(chunk.size());
    }

    out.flush();
    if (!out.good())
    {
        outError = "Failed to flush embedded output file: " + destination.string();
        return false;
    }

    u64 hashedSize = 0;
    std::vector<u8> prefix;
    if (!computeSha1AndPrefix(destination, outSha1, hashedSize, prefix, outError))
    {
        return false;
    }
    if (hashedSize != size)
    {
        outError = "Embedded carve hash size mismatch for file: " + destination.string();
        return false;
    }
    return true;
}

bool isLikelyPsxExe(const std::vector<u8>& data, u64 fileSize)
{
    if (fileSize < iso::PsxExeLoader::kHeaderSize || data.size() < 8)
    {
        return false;
    }
    return std::memcmp(data.data(), "PS-X EXE", 8) == 0;
}

bool isLikelyTim(const std::vector<u8>& data, u64 fileSize)
{
    if (fileSize < 20 || data.size() < 12)
    {
        return false;
    }

    if (readLe32(data, 0) != 0x00000010U)
    {
        return false;
    }

    const u32 flags = readLe32(data, 4);
    if ((flags & ~0x0000000BU) != 0)
    {
        return false;
    }

    u64 offset = 8;
    auto parseBlock = [&](u64 blockOffset, u32& outBlockSize) -> bool
    {
        if (blockOffset + 4 > static_cast<u64>(data.size()))
        {
            return false;
        }
        outBlockSize = readLe32(data, static_cast<size_t>(blockOffset));
        if (outBlockSize < 12)
        {
            return false;
        }
        return blockOffset + static_cast<u64>(outBlockSize) <= fileSize;
    };

    if ((flags & 0x08U) != 0)
    {
        u32 clutBlockSize = 0;
        if (!parseBlock(offset, clutBlockSize))
        {
            return false;
        }
        offset += clutBlockSize;
    }

    u32 imageBlockSize = 0;
    if (!parseBlock(offset, imageBlockSize))
    {
        return false;
    }
    return imageBlockSize != 0;
}

} // namespace

std::vector<std::string> detectCatalogTypes(const std::string& isoPath, const std::vector<u8>& data,
                                            u64 fileSize, bool hasRawXaMetadata,
                                            bool forceExecutableType)
{
    std::vector<std::string> detectedTypes;

    if (isLikelyPsxExe(data, fileSize) || forceExecutableType)
    {
        detectedTypes.push_back("psx_exe");
    }
    if (isLikelyTim(data, fileSize))
    {
        detectedTypes.push_back("tim");
    }
    if (hasExtension(isoPath, ".str") && fileSize >= 2048)
    {
        detectedTypes.push_back("str");
    }
    if (hasExtension(isoPath, ".xa"))
    {
        detectedTypes.push_back(hasRawXaMetadata ? "xa" : "xa_maybe");
    }

    std::sort(detectedTypes.begin(), detectedTypes.end());
    detectedTypes.erase(std::unique(detectedTypes.begin(), detectedTypes.end()),
                        detectedTypes.end());
    return detectedTypes;
}

bool scanEmbeddedTimResources(iso::IsoParser& parser, const std::vector<iso::IsoFileEntry>& entries,
                              const std::filesystem::path& resourcesRoot,
                              const PipelineOptions::ResourceExportOptions& options,
                              std::vector<std::string>& warnings,
                              std::vector<std::string>& resourceManifestWarnings,
                              EmbeddedScanSummary& summary, std::vector<EmbeddedHit>& outHits,
                              std::string& outError)
{
    summary.enabled = options.enableEmbeddedScan;
    summary.containersScanned = 0;
    summary.hitsExtracted = 0;
    outHits.clear();

    if (!options.enableEmbeddedScan)
    {
        return true;
    }

    size_t containersVisited = 0;
    for (const auto& entry : entries)
    {
        if (entry.isDirectory || entry.size < kEmbeddedScanMinContainerBytes ||
            !isLikelyContainerPath(entry.path))
        {
            continue;
        }
        if (containersVisited >= kEmbeddedScanMaxContainers)
        {
            break;
        }

        ++containersVisited;
        ++summary.containersScanned;

        const u64 scanLimit =
            std::min<u64>(static_cast<u64>(entry.size), kEmbeddedScanMaxBytesPerContainer);
        const std::string containerId = buildContainerId(entry.path);
        const std::filesystem::path containerOutDir =
            resourcesRoot / "embedded" / "by_container" / containerId / "tim";

        u64 cursor = 0;
        u64 skipUntil = 0;
        size_t hitsInContainer = 0;
        std::vector<u8> tail;
        tail.reserve(kEmbeddedScanOverlapBytes);

        while (cursor < scanLimit && hitsInContainer < kEmbeddedScanMaxHitsPerContainer)
        {
            const size_t readSize =
                static_cast<size_t>(std::min<u64>(scanLimit - cursor, kEmbeddedScanChunkBytes));
            std::vector<u8> chunk;
            std::string readError;
            if (!parser.readRangeFromIsoFile(entry.path, cursor, readSize, chunk, &readError))
            {
                const std::string warning = "Embedded scan skipped remainder of container '" +
                                            entry.path + "': " + readError;
                warnings.push_back(warning);
                resourceManifestWarnings.push_back(warning);
                break;
            }

            std::vector<u8> combined;
            combined.reserve(tail.size() + chunk.size());
            combined.insert(combined.end(), tail.begin(), tail.end());
            combined.insert(combined.end(), chunk.begin(), chunk.end());

            const long long combinedBase =
                static_cast<long long>(cursor) - static_cast<long long>(tail.size());
            for (size_t i = 0; i + 4 <= combined.size(); ++i)
            {
                const long long absoluteOffsetSigned = combinedBase + static_cast<long long>(i);
                if (absoluteOffsetSigned < 0)
                {
                    continue;
                }
                const u64 absoluteOffset = static_cast<u64>(absoluteOffsetSigned);
                if (absoluteOffset + 4 > scanLimit || absoluteOffset < skipUntil)
                {
                    continue;
                }

                if (!(combined[i] == 0x10 && combined[i + 1] == 0x00 && combined[i + 2] == 0x00 &&
                      combined[i + 3] == 0x00))
                {
                    continue;
                }

                u64 timSize = 0;
                std::string parseError;
                if (!parseTimBlobAtOffset(parser, entry.path, absoluteOffset, scanLimit,
                                          static_cast<u64>(entry.size), timSize, parseError))
                {
                    continue;
                }

                const std::string baseName = embeddedBlobBaseName(absoluteOffset, timSize);
                const std::filesystem::path tempPath = containerOutDir / (baseName + "_tmp.tim");
                std::string blobSha1;
                std::string carveError;
                if (!carveRangeToFileAndHash(parser, entry.path, absoluteOffset, timSize, tempPath,
                                             blobSha1, carveError))
                {
                    outError = "Embedded carve failed for '" + entry.path + "': " + carveError;
                    return false;
                }

                const std::string hash8 = blobSha1.substr(0, std::min<size_t>(8, blobSha1.size()));
                const std::filesystem::path finalPath =
                    containerOutDir / (baseName + "_" + hash8 + ".tim");
                std::error_code renameError;
                std::filesystem::rename(tempPath, finalPath, renameError);
                if (renameError)
                {
                    if (std::filesystem::exists(finalPath))
                    {
                        std::error_code cleanupError;
                        std::filesystem::remove(tempPath, cleanupError);
                    }
                    else
                    {
                        outError = "Failed to finalize embedded carve file '" + finalPath.string() +
                                   "': " + renameError.message();
                        return false;
                    }
                }

                EmbeddedHit hit;
                hit.containerIsoPath = entry.path;
                hit.offset = absoluteOffset;
                hit.size = timSize;
                hit.sha1 = blobSha1;
                hit.outputPath =
                    std::filesystem::relative(finalPath, resourcesRoot).generic_string();
                outHits.push_back(std::move(hit));

                ++hitsInContainer;
                ++summary.hitsExtracted;
                skipUntil = absoluteOffset + timSize;
            }

            const size_t tailSize = std::min<size_t>(kEmbeddedScanOverlapBytes, chunk.size());
            tail.assign(chunk.end() - static_cast<std::ptrdiff_t>(tailSize), chunk.end());
            cursor += static_cast<u64>(chunk.size());
        }
    }

    return true;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
