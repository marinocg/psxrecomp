#include "pipeline_helpers.h"

#include "psxrecomp/iso/iso_parser.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <utility>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{
std::string summarizePath(const std::string& value)
{
    if (value.empty())
    {
        return value;
    }

    std::filesystem::path path(value);
    const std::string filename = path.filename().string();
    return filename.empty() ? value : filename;
}

std::string escapeJson(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value)
    {
        switch (ch)
        {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

std::string trackTypeToString(iso::TrackType type)
{
    switch (type)
    {
    case iso::TrackType::Data:
        return "data";
    case iso::TrackType::Audio:
        return "audio";
    default:
        return "unknown";
    }
}

std::string serializeDiscTree(const std::vector<iso::IsoFileEntry>& entries)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schemaVersion\": \"1.0\",\n";
    stream << "  \"root\": \"\",\n";
    stream << "  \"entries\": [\n";
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries[i];
        stream << "    {\n";
        stream << "      \"path\": \"" << escapeJson(entry.path) << "\",\n";
        stream << "      \"type\": \"" << (entry.isDirectory ? "dir" : "file") << "\",\n";
        if (!entry.isDirectory)
        {
            stream << "      \"bytes\": " << entry.size << ",\n";
        }
        stream << "      \"flags\": \"0x" << formatHex(entry.flags, 2) << "\",\n";
        stream << "      \"extents\": [\n";
        for (size_t extentIndex = 0; extentIndex < entry.extents.size(); ++extentIndex)
        {
            const auto& extent = entry.extents[extentIndex];
            stream << "        {\n";
            stream << "          \"lba\": " << extent.lba << ",\n";
            stream << "          \"bytes\": " << extent.size << ",\n";
            stream << "          \"continues\": " << (extent.continues ? "true" : "false") << "\n";
            stream << "        }";
            if (extentIndex + 1 < entry.extents.size())
            {
                stream << ",";
            }
            stream << "\n";
        }
        stream << "      ]\n";
        stream << "    }";
        if (i + 1 < entries.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ]\n";
    stream << "}\n";
    return stream.str();
}

std::string serializeDiscMeta(const std::string& inputPath, const std::string& bootExecutable,
                              const iso::IsoParser& parser,
                              const std::vector<iso::IsoFileEntry>& entries)
{
    size_t fileCount = 0;
    size_t directoryCount = 0;
    for (const auto& entry : entries)
    {
        if (entry.isDirectory)
        {
            ++directoryCount;
        }
        else
        {
            ++fileCount;
        }
    }

    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schemaVersion\": \"1.0\",\n";
    stream << "  \"inputPath\": \"" << escapeJson(summarizePath(inputPath)) << "\",\n";
    stream << "  \"volumeLabel\": \"" << escapeJson(parser.getVolumeLabel()) << "\",\n";
    stream << "  \"isJoliet\": " << (parser.isUsingJoliet() ? "true" : "false") << ",\n";
    stream << "  \"rawSectorSize\": " << parser.getRawSectorSize() << ",\n";
    stream << "  \"logicalBlockSize\": " << parser.getLogicalBlockSize() << ",\n";
    stream << "  \"totalSectors\": " << parser.getTotalSectors() << ",\n";
    stream << "  \"bootExecutable\": \"" << escapeJson(bootExecutable) << "\",\n";
    stream << "  \"tracks\": [\n";
    const auto& tracks = parser.getTracks();
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& track = tracks[i];
        stream << "    {\n";
        stream << "      \"trackNumber\": " << track.trackNumber << ",\n";
        stream << "      \"type\": \"" << trackTypeToString(track.type) << "\",\n";
        stream << "      \"sectorSize\": " << track.sectorSize << ",\n";
        stream << "      \"startLba\": " << track.startLba << ",\n";
        stream << "      \"pregapLength\": " << track.pregapLength << ",\n";
        stream << "      \"sessionNumber\": " << track.sessionNumber << "\n";
        stream << "    }";
        if (i + 1 < tracks.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ],\n";
    const auto dataTrack = parser.getDataTrack();
    stream << "  \"dataTrack\": ";
    if (dataTrack.has_value())
    {
        stream << "{\n";
        stream << "    \"trackNumber\": " << dataTrack->trackNumber << ",\n";
        stream << "    \"startLba\": " << dataTrack->startLba << ",\n";
        stream << "    \"sectorSize\": " << dataTrack->sectorSize << "\n";
        stream << "  },\n";
    }
    else
    {
        stream << "null,\n";
    }
    stream << "  \"stats\": {\n";
    stream << "    \"discFiles\": " << fileCount << ",\n";
    stream << "    \"discDirs\": " << directoryCount << "\n";
    stream << "  }\n";
    stream << "}\n";
    return stream.str();
}

std::string serializeResourcesManifest(const std::string& inputPath, const std::string& timestamp,
                                       const std::string& pipelineVersion,
                                       const iso::IsoParser& parser,
                                       const std::vector<iso::IsoFileEntry>& entries,
                                       size_t filesystemExportedCount, u64 filesystemExportedBytes,
                                       const std::vector<std::string>& extraWarnings)
{
    std::vector<const iso::IsoFileEntry*> files;
    files.reserve(entries.size());
    size_t discFileCount = 0;
    size_t discDirCount = 0;
    for (const auto& entry : entries)
    {
        if (entry.isDirectory)
        {
            ++discDirCount;
            continue;
        }
        ++discFileCount;
        files.push_back(&entry);
    }

    std::sort(files.begin(), files.end(),
              [](const iso::IsoFileEntry* lhs, const iso::IsoFileEntry* rhs)
              { return lhs->size > rhs->size; });

    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schemaVersion\": \"1.0\",\n";
    stream << "  \"generatedAt\": \"" << escapeJson(timestamp) << "\",\n";
    stream << "  \"pipelineVersion\": \"" << escapeJson(pipelineVersion) << "\",\n";
    stream << "  \"disc\": {\n";
    stream << "    \"inputPath\": \"" << escapeJson(summarizePath(inputPath)) << "\",\n";
    stream << "    \"volumeLabel\": \"" << escapeJson(parser.getVolumeLabel()) << "\",\n";
    stream << "    \"isJoliet\": " << (parser.isUsingJoliet() ? "true" : "false") << ",\n";
    stream << "    \"rawSectorSize\": " << parser.getRawSectorSize() << ",\n";
    stream << "    \"logicalBlockSize\": " << parser.getLogicalBlockSize() << "\n";
    stream << "  },\n";
    stream << "  \"index\": {\n";
    stream << "    \"discTreePath\": \"index/disc_tree.json\",\n";
    stream << "    \"discMetaPath\": \"index/disc_meta.json\",\n";
    stream << "    \"resourcesManifestPath\": \"index/resources_manifest.json\"\n";
    stream << "  },\n";
    stream << "  \"exports\": {\n";
    stream << "    \"filesystem\": {\n";
    stream << "      \"enabled\": true,\n";
    stream << "      \"root\": \"fs\",\n";
    stream << "      \"result\": {\n";
    stream << "        \"filesExported\": " << filesystemExportedCount << ",\n";
    stream << "        \"bytesExported\": " << filesystemExportedBytes << ",\n";
    stream << "        \"skippedDueToLimits\": 0\n";
    stream << "      }\n";
    stream << "    },\n";
    stream << "    \"embeddedScan\": {\n";
    stream << "      \"enabled\": false,\n";
    stream << "      \"result\": {\n";
    stream << "        \"containersScanned\": 0,\n";
    stream << "        \"hitsExtracted\": 0\n";
    stream << "      }\n";
    stream << "    },\n";
    stream << "    \"derived\": {\n";
    stream << "      \"timToPng\": { \"enabled\": false },\n";
    stream << "      \"xaToWav\": { \"enabled\": false }\n";
    stream << "    }\n";
    stream << "  },\n";
    stream << "  \"runtimeSummary\": {\n";
    stream << "    \"filesystemEnabled\": true,\n";
    stream << "    \"containersScanned\": 0,\n";
    stream << "    \"hitsExtracted\": 0\n";
    stream << "  },\n";
    stream << "  \"stats\": {\n";
    stream << "    \"discFiles\": " << discFileCount << ",\n";
    stream << "    \"discDirs\": " << discDirCount << ",\n";
    stream << "    \"largestFiles\": [\n";
    const size_t largestCount = std::min<size_t>(3, files.size());
    for (size_t i = 0; i < largestCount; ++i)
    {
        stream << "      {\n";
        stream << "        \"path\": \"" << escapeJson(files[i]->path) << "\",\n";
        stream << "        \"bytes\": " << files[i]->size << "\n";
        stream << "      }";
        if (i + 1 < largestCount)
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "    ]\n";
    stream << "  },\n";
    stream << "  \"warnings\": [\n";
    std::vector<std::string> warnings = extraWarnings;
    if (filesystemExportedCount == 0)
    {
        warnings.push_back(
            "No loose .TIM/.STR/.XA resources were exported. Assets may be packed in containers.");
    }
    for (size_t i = 0; i < warnings.size(); ++i)
    {
        stream << "    \"" << escapeJson(warnings[i]) << "\"";
        if (i + 1 < warnings.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ]\n";
    stream << "}\n";
    return stream.str();
}
} // namespace

bool writeOutputArtifacts(PipelineResult& result, const std::filesystem::path& outputDir,
                          const std::string& moduleName, const std::string& header,
                          const std::string& source, const std::string& runnerSource,
                          const std::string& buildFile, const std::string& activeDiscPath,
                          const std::filesystem::path& inputFsPath,
                          std::vector<std::string>& warnings,
                          std::vector<PipelineDiagnostic>& diagnostics,
                          const std::string& manifestTimestamp, const std::string& pipelineVersion,
                          std::string& outError)
{
    std::error_code dirError;
    std::filesystem::create_directories(outputDir, dirError);
    if (dirError)
    {
        outError = "Failed to create output directory: " + outputDir.string();
        return false;
    }

    PipelineArtifacts artifacts;
    artifacts.moduleName = moduleName;
    artifacts.headerPath = (outputDir / (moduleName + ".h")).string();
    artifacts.sourcePath = (outputDir / (moduleName + ".cpp")).string();
    const std::string runnerPath = (outputDir / (moduleName + "_runner.cpp")).string();
    artifacts.buildPath = (outputDir / "CMakeLists.txt").string();
    artifacts.manifestPath = (outputDir / "manifest.json").string();
    artifacts.resourcesPath = (outputDir / "resources").string();
    artifacts.resourceRootPath = artifacts.resourcesPath;
    artifacts.resourceManifestPath = "";
    artifacts.runtimeIncludePath = (outputDir / "runtime" / "include").string();
    artifacts.runtimeSourcePath = (outputDir / "runtime" / "src").string();
    const std::string timestamp = buildTimestamp(manifestTimestamp);

    if (!writeFile(artifacts.headerPath, header, outError) ||
        !writeFile(artifacts.sourcePath, source, outError) ||
        !writeFile(runnerPath, runnerSource, outError) ||
        !writeFile(artifacts.buildPath, buildFile, outError))
    {
        return false;
    }

    std::filesystem::path repoRoot = repositoryRootFromSourcePath(std::filesystem::path(__FILE__));
    if (!copyDirectoryRecursive(repoRoot / "include" / "psxrecomp",
                                std::filesystem::path(artifacts.runtimeIncludePath) / "psxrecomp",
                                outError) ||
        !copyDirectoryRecursive(repoRoot / "src" / "runtime", artifacts.runtimeSourcePath,
                                outError))
    {
        return false;
    }

    if (isIsoLikePath(inputFsPath))
    {
        iso::IsoParser parser(activeDiscPath);
        if (parser.open() && parser.isValid())
        {
            const std::filesystem::path resourcesRoot(artifacts.resourcesPath);
            const std::filesystem::path resourcesIndex = resourcesRoot / "index";
            const std::filesystem::path resourcesFs = resourcesRoot / "fs";

            std::filesystem::create_directories(resourcesIndex, dirError);
            if (dirError)
            {
                outError = "Failed to create resources directory: " + artifacts.resourcesPath;
                return false;
            }
            std::filesystem::remove_all(resourcesFs, dirError);
            if (dirError)
            {
                outError =
                    "Failed to clear filesystem resources directory: " + resourcesFs.string();
                return false;
            }
            std::filesystem::create_directories(resourcesFs, dirError);
            if (dirError)
            {
                outError =
                    "Failed to create filesystem resources directory: " + resourcesFs.string();
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
            if (!writeFile(
                    discMetaPath,
                    serializeDiscMeta(activeDiscPath, bootExecutable, parser, isoTreeEntries),
                    outError))
            {
                return false;
            }
            artifacts.exportedResources.push_back("index/disc_tree.json");
            artifacts.exportedResources.push_back("index/disc_meta.json");

            std::vector<std::pair<iso::ResourceType, std::string>> resourceTypes = {
                {iso::ResourceType::TimTexture, "TIM"},
                {iso::ResourceType::StrVideo, "STR"},
                {iso::ResourceType::XaAudio, "XA"},
            };
            std::vector<std::string> resourceManifestWarnings;
            for (const auto& [type, label] : resourceTypes)
            {
                if (!parser.exportResources(type, resourcesFs.string()))
                {
                    const std::string warning =
                        "Resource export reported errors for " + label + ".";
                    warnings.push_back(warning);
                    resourceManifestWarnings.push_back(warning);
                }
            }

            size_t filesystemExportedCount = 0;
            u64 exportedResourceBytes = 0;
            try
            {
                for (const auto& exportedEntry :
                     std::filesystem::recursive_directory_iterator(resourcesFs))
                {
                    if (!exportedEntry.is_regular_file())
                    {
                        continue;
                    }

                    const auto relativePath =
                        std::filesystem::relative(exportedEntry.path(), resourcesRoot);
                    artifacts.exportedResources.push_back(relativePath.generic_string());
                    ++filesystemExportedCount;
                    exportedResourceBytes += static_cast<u64>(exportedEntry.file_size());
                }
            }
            catch (const std::filesystem::filesystem_error& error)
            {
                const std::string warning =
                    std::string("Failed to enumerate exported filesystem resources: ") +
                    error.what();
                warnings.push_back(warning);
                resourceManifestWarnings.push_back(warning);
            }

            const auto resourcesManifestPath = resourcesIndex / "resources_manifest.json";
            if (!writeFile(
                    resourcesManifestPath,
                    serializeResourcesManifest(activeDiscPath, timestamp, pipelineVersion, parser,
                                               isoTreeEntries, filesystemExportedCount,
                                               exportedResourceBytes, resourceManifestWarnings),
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
        }
        else
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
        }
    }

    result.success = true;
    result.artifacts = artifacts;
    result.warnings = warnings;
    result.diagnostics = diagnostics;

    const std::string manifest =
        serializeManifest(result, activeDiscPath, outputDir.string(), timestamp, pipelineVersion);
    if (!writeFile(artifacts.manifestPath, manifest, outError))
    {
        return false;
    }
    return true;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
