#include "pipeline_helpers_output_support.h"

#include "pipeline_helpers_output_model.h"
#include "pipeline_selection_helpers.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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

constexpr size_t kSha1BlockSize = 64;
constexpr size_t kSha1DigestSize = 20;
constexpr size_t kSniffPrefixBytes = 4096;

u32 rotateLeft(u32 value, u32 amount)
{
    return (value << amount) | (value >> (32 - amount));
}

u32 readLe32(const std::vector<u8>& data, size_t offset)
{
    return static_cast<u32>(data[offset]) | (static_cast<u32>(data[offset + 1]) << 8) |
           (static_cast<u32>(data[offset + 2]) << 16) | (static_cast<u32>(data[offset + 3]) << 24);
}

class Sha1Hasher
{
  public:
    Sha1Hasher()
    {
        m_state = {0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};
        m_totalBytes = 0;
        m_bufferSize = 0;
    }

    void update(const u8* data, size_t size)
    {
        if (size == 0)
        {
            return;
        }

        m_totalBytes += static_cast<u64>(size);
        size_t offset = 0;
        while (offset < size)
        {
            const size_t toCopy = std::min(kSha1BlockSize - m_bufferSize, size - offset);
            std::memcpy(m_buffer.data() + m_bufferSize, data + offset, toCopy);
            m_bufferSize += toCopy;
            offset += toCopy;

            if (m_bufferSize == kSha1BlockSize)
            {
                processBlock(m_buffer.data());
                m_bufferSize = 0;
            }
        }
    }

    std::array<u8, kSha1DigestSize> finalize()
    {
        const u64 bitLength = m_totalBytes * 8ULL;

        m_buffer[m_bufferSize++] = 0x80;
        if (m_bufferSize > 56)
        {
            std::fill(m_buffer.begin() + static_cast<std::ptrdiff_t>(m_bufferSize), m_buffer.end(),
                      0);
            processBlock(m_buffer.data());
            m_bufferSize = 0;
        }

        std::fill(m_buffer.begin() + static_cast<std::ptrdiff_t>(m_bufferSize),
                  m_buffer.begin() + static_cast<std::ptrdiff_t>(56), 0);
        for (size_t i = 0; i < 8; ++i)
        {
            m_buffer[56 + i] = static_cast<u8>((bitLength >> ((7 - i) * 8)) & 0xFFULL);
        }
        processBlock(m_buffer.data());
        m_bufferSize = 0;

        std::array<u8, kSha1DigestSize> digest{};
        for (size_t i = 0; i < m_state.size(); ++i)
        {
            digest[i * 4] = static_cast<u8>((m_state[i] >> 24) & 0xFFU);
            digest[i * 4 + 1] = static_cast<u8>((m_state[i] >> 16) & 0xFFU);
            digest[i * 4 + 2] = static_cast<u8>((m_state[i] >> 8) & 0xFFU);
            digest[i * 4 + 3] = static_cast<u8>(m_state[i] & 0xFFU);
        }
        return digest;
    }

  private:
    void processBlock(const u8* block)
    {
        std::array<u32, 80> words{};
        for (size_t i = 0; i < 16; ++i)
        {
            const size_t offset = i * 4;
            words[i] = (static_cast<u32>(block[offset]) << 24) |
                       (static_cast<u32>(block[offset + 1]) << 16) |
                       (static_cast<u32>(block[offset + 2]) << 8) |
                       static_cast<u32>(block[offset + 3]);
        }
        for (size_t i = 16; i < words.size(); ++i)
        {
            words[i] = rotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
        }

        u32 a = m_state[0];
        u32 b = m_state[1];
        u32 c = m_state[2];
        u32 d = m_state[3];
        u32 e = m_state[4];

        for (size_t i = 0; i < words.size(); ++i)
        {
            u32 f = 0;
            u32 k = 0;
            if (i < 20)
            {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999U;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1U;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCU;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6U;
            }

            const u32 temp = rotateLeft(a, 5) + f + e + k + words[i];
            e = d;
            d = c;
            c = rotateLeft(b, 30);
            b = a;
            a = temp;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
    }

    std::array<u32, 5> m_state{};
    std::array<u8, kSha1BlockSize> m_buffer{};
    u64 m_totalBytes = 0;
    size_t m_bufferSize = 0;
};

std::string toHex(const std::array<u8, kSha1DigestSize>& digest)
{
    static const char* kHex = "0123456789abcdef";
    std::string value;
    value.reserve(kSha1DigestSize * 2);
    for (u8 byte : digest)
    {
        value.push_back(kHex[(byte >> 4) & 0x0F]);
        value.push_back(kHex[byte & 0x0F]);
    }
    return value;
}

std::string computeSha1Hex(const u8* data, size_t size)
{
    Sha1Hasher hasher;
    hasher.update(data, size);
    return toHex(hasher.finalize());
}

bool validateSha1Implementation(std::string& outError)
{
    struct TestVector
    {
        std::string input;
        std::string expectedSha1;
    };

    const std::array<TestVector, 4> testVectors = {{
        {"", "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {"abc", "a9993e364706816aba3e25717850c26c9cd0d89d"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "84983e441c3bd26ebaae4aa1f95129e5e54670f1"},
        {std::string(1000000, 'a'), "34aa973cd4c4daa4f61eeb2bdbad27316534016f"},
    }};

    for (const auto& vector : testVectors)
    {
        const auto* bytes = reinterpret_cast<const u8*>(vector.input.data());
        const std::string digest = computeSha1Hex(bytes, vector.input.size());
        if (digest != vector.expectedSha1)
        {
            outError = "Expected " + vector.expectedSha1 + " but got " + digest + ".";
            return false;
        }
    }

    outError.clear();
    return true;
}

bool ensureSha1SelfTest(std::string& outError)
{
    struct SelfTestState
    {
        bool passed = false;
        std::string error;
    };

    static const SelfTestState state = []
    {
        SelfTestState result;
        result.passed = validateSha1Implementation(result.error);
        return result;
    }();

    if (!state.passed)
    {
        outError = "SHA-1 self-test failed: " + state.error;
        return false;
    }
    outError.clear();
    return true;
}

bool computeSha1AndPrefix(const std::filesystem::path& path, std::string& outSha1, u64& outSize,
                          std::vector<u8>& outPrefix, std::string& outError)
{
    outSha1.clear();
    outSize = 0;
    outPrefix.clear();

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        outError = "Failed to open file for hashing: " + path.string();
        return false;
    }

    Sha1Hasher hasher;
    std::array<char, 8192> buffer{};
    while (input.good())
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize readCount = input.gcount();
        if (readCount <= 0)
        {
            break;
        }

        hasher.update(reinterpret_cast<const u8*>(buffer.data()), static_cast<size_t>(readCount));
        outSize += static_cast<u64>(readCount);

        if (outPrefix.size() < kSniffPrefixBytes)
        {
            const size_t remainingPrefix = kSniffPrefixBytes - outPrefix.size();
            const size_t copyCount = std::min(remainingPrefix, static_cast<size_t>(readCount));
            outPrefix.insert(outPrefix.end(), reinterpret_cast<const u8*>(buffer.data()),
                             reinterpret_cast<const u8*>(buffer.data()) + copyCount);
        }
    }

    if (!input.eof() && input.fail())
    {
        outError = "Failed to read file for hashing: " + path.string();
        return false;
    }

    outSha1 = toHex(hasher.finalize());
    return true;
}

bool hasExtension(const std::string& path, const std::string& extensionLower)
{
    std::filesystem::path fsPath(path);
    std::string value = toLower(fsPath.extension().string());
    return value == extensionLower;
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

} // namespace

bool exportIsoResourceArtifacts(PipelineArtifacts& artifacts, const std::string& activeDiscPath,
                                const PipelineOptions::ResourceExportOptions& resourceExportOptions,
                                std::vector<std::string>& warnings,
                                std::vector<PipelineDiagnostic>& diagnostics,
                                const std::string& timestamp, const std::string& pipelineVersion,
                                std::string& outError)
{
    if (!ensureSha1SelfTest(outError))
    {
        return false;
    }

    iso::IsoParser parser(activeDiscPath);
    if (!(parser.open() && parser.isValid()))
    {
        for (const auto& error : parser.getErrors())
        {
            PipelineDiagnostic entry;
            entry.code = "IsoParserError";
            entry.severity = "error";
            entry.message = "Resource export skipped: " + error;
            entry.context.file = activeDiscPath;
            diagnostics.push_back(entry);
        }
        warnings.push_back("Resource export skipped due to parser errors.");
        return true;
    }

    std::error_code dirError;
    const std::filesystem::path resourcesRoot(artifacts.resourcesPath);
    const std::filesystem::path resourcesIndex = resourcesRoot / "index";
    const std::filesystem::path resourcesFs = resourcesRoot / "fs";

    std::filesystem::create_directories(resourcesIndex, dirError);
    if (dirError)
    {
        outError = "Failed to create resources index directory: " + resourcesIndex.string() + ": " +
                   dirError.message();
        return false;
    }
    std::filesystem::remove_all(resourcesFs, dirError);
    if (dirError)
    {
        outError = "Failed to clear filesystem resources directory: " + resourcesFs.string() +
                   ": " + dirError.message();
        return false;
    }
    std::filesystem::create_directories(resourcesFs, dirError);
    if (dirError)
    {
        outError = "Failed to create filesystem resources directory: " + resourcesFs.string() +
                   ": " + dirError.message();
        return false;
    }

    const auto isoTreeEntries = parser.listAllFilesRecursive();
    const auto bootExecutable = parser.findExecutable();
    const auto discTreePath = resourcesIndex / "disc_tree.json";
    const auto discMetaPath = resourcesIndex / "disc_meta.json";
    if (!writeFile(discTreePath, serializeDiscTree(isoTreeEntries), outError))
    {
        return false;
    }
    if (!writeFile(discMetaPath,
                   serializeDiscMeta(activeDiscPath, bootExecutable, parser, isoTreeEntries),
                   outError))
    {
        return false;
    }
    artifacts.exportedResources.push_back("index/disc_tree.json");
    artifacts.exportedResources.push_back("index/disc_meta.json");

    FilesystemExportSummary filesystemSummary;
    filesystemSummary.mode = fsModeToString(resourceExportOptions.fsMode);
    filesystemSummary.maxTotalBytes = resourceExportOptions.maxTotalBytes;
    filesystemSummary.maxSingleFileBytes = resourceExportOptions.maxSingleFileBytes;
    std::vector<std::string> resourceManifestWarnings;
    std::unordered_map<std::string, std::string> exportedPathByIsoPath;
    std::unordered_map<std::string, const iso::IsoFileEntry*> fileEntryByPath;
    for (const auto& entry : isoTreeEntries)
    {
        if (entry.isDirectory)
        {
            continue;
        }
        fileEntryByPath.emplace(toLower(entry.path), &entry);
    }

    std::vector<std::string> executablePaths = parser.listExecutables();
    if (!bootExecutable.empty())
    {
        executablePaths.push_back(bootExecutable);
    }
    const std::vector<std::string> uniqueExecutablePaths =
        deduplicatePathsCaseInsensitive(executablePaths);
    std::unordered_set<std::string> executablePathSet;
    for (const auto& executablePath : uniqueExecutablePaths)
    {
        executablePathSet.insert(toLower(executablePath));
    }

    const FilesystemExportPlan exportPlan =
        buildFilesystemExportPlan(isoTreeEntries, bootExecutable, resourceExportOptions);
    filesystemSummary.skippedDueToLimits = exportPlan.skippedDueToLimits;
    filesystemSummary.alwaysIncludedRules = exportPlan.alwaysIncludedRules;

    struct ExportedFsFile
    {
        std::string isoPath;
        std::string exportedPath;
    };
    std::vector<ExportedFsFile> exportedFsFiles;
    exportedFsFiles.reserve(exportPlan.paths.size());

    for (const auto& isoRelativePath : exportPlan.paths)
    {
        std::string exportError;
        const std::filesystem::path destination =
            resourcesFs / std::filesystem::path(isoRelativePath);
        if (!parser.exportFileTo(isoRelativePath, destination, &exportError))
        {
            const std::string warning = "Failed to export ISO file '" + isoRelativePath +
                                        "' to resources/fs: " + exportError;
            warnings.push_back(warning);
            resourceManifestWarnings.push_back(warning);
            continue;
        }

        const std::string exportedPath =
            (std::filesystem::path("fs") / std::filesystem::path(isoRelativePath)).generic_string();
        artifacts.exportedResources.push_back(exportedPath);
        exportedPathByIsoPath.emplace(toLower(isoRelativePath), exportedPath);
        exportedFsFiles.push_back({isoRelativePath, exportedPath});
        ++filesystemSummary.filesExported;
        const auto entryIt = fileEntryByPath.find(toLower(isoRelativePath));
        if (entryIt != fileEntryByPath.end())
        {
            filesystemSummary.bytesExported += static_cast<u64>(entryIt->second->size);
        }
    }

    if (filesystemSummary.filesExported == 0)
    {
        const std::string warning =
            "Filesystem export produced no files. Check export policy and ISO contents.";
        warnings.push_back(warning);
        resourceManifestWarnings.push_back(warning);
    }

    auto findExportedPath = [&](const std::string& isoPath) -> std::string
    {
        if (isoPath.empty())
        {
            return "";
        }
        const auto it = exportedPathByIsoPath.find(toLower(isoPath));
        if (it == exportedPathByIsoPath.end())
        {
            return "";
        }
        return it->second;
    };

    std::vector<RecompInputExecutable> recompInputExecutables;
    recompInputExecutables.reserve(uniqueExecutablePaths.size());

    for (const auto& executablePath : uniqueExecutablePaths)
    {
        const std::vector<u8> executableData = parser.extractFile(executablePath);
        if (executableData.empty())
        {
            const std::string warning = "Recomp inputs metadata skipped for executable '" +
                                        executablePath + "' because extraction failed.";
            warnings.push_back(warning);
            resourceManifestWarnings.push_back(warning);
            continue;
        }

        iso::PsxExeHeader executableHeader{};
        iso::PsxExeDiagnostics executableDiagnostics;
        if (!iso::PsxExeLoader::parseHeader(executableData, executableHeader,
                                            &executableDiagnostics))
        {
            const std::string warning = "Recomp inputs metadata skipped for executable '" +
                                        executablePath + "' because PS-X EXE parsing failed.";
            warnings.push_back(warning);
            resourceManifestWarnings.push_back(warning);
            continue;
        }

        u32 effectiveLoadSize = executableHeader.loadSize;
        if (effectiveLoadSize == 0 && executableData.size() >= iso::PsxExeLoader::kHeaderSize)
        {
            effectiveLoadSize =
                static_cast<u32>(executableData.size() - iso::PsxExeLoader::kHeaderSize);
        }

        RecompInputExecutable entry;
        entry.isoPath = executablePath;
        entry.exportedPath = findExportedPath(executablePath);
        entry.loadAddress = executableHeader.loadAddress;
        entry.loadSize = effectiveLoadSize;
        entry.entryPoint = executableHeader.initialPc;
        entry.gp = executableHeader.initialGp;
        entry.bssAddress = executableHeader.bssAddress;
        entry.bssSize = executableHeader.bssSize;
        entry.stackAddress = executableHeader.stackAddress;
        entry.stackSize = executableHeader.stackSize;
        recompInputExecutables.push_back(std::move(entry));
    }

    std::sort(recompInputExecutables.begin(), recompInputExecutables.end(),
              [](const RecompInputExecutable& lhs, const RecompInputExecutable& rhs)
              {
                  const std::string lhsKey = toLower(lhs.isoPath);
                  const std::string rhsKey = toLower(rhs.isoPath);
                  if (lhsKey != rhsKey)
                  {
                      return lhsKey < rhsKey;
                  }
                  return lhs.isoPath < rhs.isoPath;
              });

    const auto recompInputsPath = resourcesIndex / "recomp_inputs.json";
    if (!writeFile(recompInputsPath,
                   serializeRecompInputs(bootExecutable, findExportedPath(bootExecutable),
                                         findExportedPath("SYSTEM.CNF"), recompInputExecutables),
                   outError))
    {
        return false;
    }
    artifacts.exportedResources.push_back("index/recomp_inputs.json");

    std::vector<CatalogEntry> catalogEntries;
    catalogEntries.reserve(exportedFsFiles.size());
    const bool hasRawXaMetadata = parser.getRawSectorSize() == 2352;
    for (const auto& exportedFile : exportedFsFiles)
    {
        const auto fileEntryIt = fileEntryByPath.find(toLower(exportedFile.isoPath));
        if (fileEntryIt == fileEntryByPath.end())
        {
            outError = "Catalog generation failed: missing ISO tree entry for '" +
                       exportedFile.isoPath + "'.";
            return false;
        }

        std::string sha1;
        u64 exportedSize = 0;
        std::vector<u8> prefix;
        std::string hashError;
        const std::filesystem::path exportedHostPath = resourcesRoot / exportedFile.exportedPath;
        if (!computeSha1AndPrefix(exportedHostPath, sha1, exportedSize, prefix, hashError))
        {
            outError = "Catalog generation failed for '" + exportedFile.isoPath + "': " + hashError;
            return false;
        }

        CatalogEntry entry;
        entry.id = "discfile:" + exportedFile.isoPath;
        entry.isoPath = exportedFile.isoPath;
        entry.exportedPath = exportedFile.exportedPath;
        entry.size = exportedSize;
        entry.sha1 = sha1;
        entry.extents = fileEntryIt->second->extents;
        entry.detectedTypes = detectCatalogTypes(
            exportedFile.isoPath, prefix, exportedSize, hasRawXaMetadata && !entry.extents.empty(),
            executablePathSet.find(toLower(exportedFile.isoPath)) != executablePathSet.end());
        catalogEntries.push_back(std::move(entry));
    }

    std::sort(catalogEntries.begin(), catalogEntries.end(),
              [](const CatalogEntry& lhs, const CatalogEntry& rhs)
              {
                  const std::string lhsKey = toLower(lhs.isoPath);
                  const std::string rhsKey = toLower(rhs.isoPath);
                  if (lhsKey != rhsKey)
                  {
                      return lhsKey < rhsKey;
                  }
                  return lhs.isoPath < rhs.isoPath;
              });

    const auto catalogPath = resourcesIndex / "catalog.json";
    if (!writeFile(catalogPath, serializeCatalog(catalogEntries), outError))
    {
        return false;
    }
    artifacts.exportedResources.push_back("index/catalog.json");

    const auto resourcesManifestPath = resourcesIndex / "resources_manifest.json";
    if (!writeFile(resourcesManifestPath,
                   serializeResourcesManifest(activeDiscPath, timestamp, pipelineVersion, parser,
                                              isoTreeEntries, filesystemSummary,
                                              resourceManifestWarnings),
                   outError))
    {
        return false;
    }
    artifacts.resourceManifestPath = resourcesManifestPath.string();
    artifacts.exportedResources.push_back("index/resources_manifest.json");

    std::sort(artifacts.exportedResources.begin(), artifacts.exportedResources.end());
    artifacts.exportedResources.erase(
        std::unique(artifacts.exportedResources.begin(), artifacts.exportedResources.end()),
        artifacts.exportedResources.end());

    return true;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
