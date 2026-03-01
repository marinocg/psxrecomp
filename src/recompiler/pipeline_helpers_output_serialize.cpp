#include "pipeline_helpers_output_model.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>

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

} // namespace

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
                                       const FilesystemExportSummary& filesystemSummary,
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
    stream << "      \"mode\": \"" << escapeJson(filesystemSummary.mode) << "\",\n";
    stream << "      \"caps\": {\n";
    stream << "        \"maxTotalBytes\": " << filesystemSummary.maxTotalBytes << ",\n";
    stream << "        \"maxSingleFileBytes\": " << filesystemSummary.maxSingleFileBytes << "\n";
    stream << "      },\n";
    stream << "      \"alwaysIncluded\": [\n";
    for (size_t i = 0; i < filesystemSummary.alwaysIncludedRules.size(); ++i)
    {
        stream << "        \"" << escapeJson(filesystemSummary.alwaysIncludedRules[i]) << "\"";
        if (i + 1 < filesystemSummary.alwaysIncludedRules.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "      ],\n";
    stream << "      \"result\": {\n";
    stream << "        \"filesExported\": " << filesystemSummary.filesExported << ",\n";
    stream << "        \"bytesExported\": " << filesystemSummary.bytesExported << ",\n";
    stream << "        \"skippedDueToLimits\": " << filesystemSummary.skippedDueToLimits << "\n";
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
    stream << "    \"filesystemMode\": \"" << escapeJson(filesystemSummary.mode) << "\",\n";
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
    const std::vector<std::string> warnings = extraWarnings;
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

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
