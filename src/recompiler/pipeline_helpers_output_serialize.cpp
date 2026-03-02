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

std::string serializeDiscLayout(const iso::IsoParser& parser, const DiscBlobSummary& summary,
                                const std::vector<DiscLayoutFile>& files)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schemaVersion\": \"1.0\",\n";
    stream << "  \"format\": \"" << escapeJson(summary.format) << "\",\n";
    stream << "  \"sectorSize\": " << summary.sectorSize << ",\n";
    stream << "  \"blob\": {\n";
    stream << "    \"path\": \"" << escapeJson(summary.blobPath) << "\",\n";
    stream << "    \"sectorSize\": " << summary.sectorSize << ",\n";
    stream << "    \"lbaStart\": " << summary.lbaStart << ",\n";
    stream << "    \"lbaCount\": " << summary.lbaCount << ",\n";
    stream << "    \"bytes\": " << summary.blobBytes << ",\n";
    stream << "    \"sha1\": \"" << escapeJson(summary.blobSha1) << "\"\n";
    stream << "  },\n";
    stream << "  \"lbaRange\": {\n";
    stream << "    \"start\": " << summary.lbaStart << ",\n";
    stream << "    \"count\": " << summary.lbaCount << "\n";
    stream << "  },\n";
    stream << "  \"volume\": {\n";
    stream << "    \"label\": \"" << escapeJson(parser.getVolumeLabel()) << "\",\n";
    stream << "    \"logicalBlockSize\": " << parser.getLogicalBlockSize() << ",\n";
    stream << "    \"isJoliet\": " << (parser.isUsingJoliet() ? "true" : "false") << "\n";
    stream << "  },\n";
    stream << "  \"files\": [\n";
    for (size_t fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        const auto& file = files[fileIndex];
        stream << "    {\n";
        stream << "      \"path\": \"" << escapeJson(file.path) << "\",\n";
        stream << "      \"bytes\": " << file.bytes << ",\n";
        stream << "      \"extents\": [\n";
        for (size_t extentIndex = 0; extentIndex < file.extents.size(); ++extentIndex)
        {
            const auto& extent = file.extents[extentIndex];
            stream << "        {\n";
            stream << "          \"lba\": " << extent.lba << ",\n";
            stream << "          \"sectors\": " << extent.sectors << ",\n";
            stream << "          \"fileByteOffset\": " << extent.fileByteOffset << "\n";
            stream << "        }";
            if (extentIndex + 1 < file.extents.size())
            {
                stream << ",";
            }
            stream << "\n";
        }
        stream << "      ]\n";
        stream << "    }";
        if (fileIndex + 1 < files.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ]\n";
    stream << "}\n";
    return stream.str();
}

std::string serializeDiscHashes(const DiscBlobSummary& summary)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schemaVersion\": \"1.0\",\n";
    stream << "  \"format\": \"" << escapeJson(summary.format) << "\",\n";
    stream << "  \"sectorSize\": " << summary.sectorSize << ",\n";
    stream << "  \"lbaRange\": {\n";
    stream << "    \"start\": " << summary.lbaStart << ",\n";
    stream << "    \"count\": " << summary.lbaCount << "\n";
    stream << "  },\n";
    stream << "  \"blob\": {\n";
    stream << "    \"path\": \"" << escapeJson(summary.blobPath) << "\",\n";
    stream << "    \"sectorSize\": " << summary.sectorSize << ",\n";
    stream << "    \"lbaStart\": " << summary.lbaStart << ",\n";
    stream << "    \"lbaCount\": " << summary.lbaCount << ",\n";
    stream << "    \"bytes\": " << summary.blobBytes << ",\n";
    stream << "    \"sha1\": \"" << escapeJson(summary.blobSha1) << "\"\n";
    stream << "  },\n";
    stream << "  \"chunks\": []\n";
    stream << "}\n";
    return stream.str();
}

std::string serializeResourcesManifest(
    const std::string& inputPath, const std::string& timestamp, const std::string& pipelineVersion,
    const iso::IsoParser& parser, const std::vector<iso::IsoFileEntry>& entries,
    const FilesystemExportSummary& filesystemSummary, const DiscBlobSummary& discBlobSummary,
    const EmbeddedScanSummary& embeddedScanSummary, const std::vector<std::string>& extraWarnings)
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
    stream << "    \"discLayoutPath\": \"disc/disc_layout.json\",\n";
    stream << "    \"discHashesPath\": \"disc/disc_hashes.json\",\n";
    stream << "    \"recompInputsPath\": \"index/recomp_inputs.json\",\n";
    stream << "    \"catalogPath\": \"index/catalog.json\",\n";
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
    stream << "    \"discBlob\": {\n";
    stream << "      \"enabled\": " << (discBlobSummary.enabled ? "true" : "false") << ",\n";
    stream << "      \"mode\": \"" << escapeJson(discBlobSummary.mode) << "\",\n";
    stream << "      \"format\": \"" << escapeJson(discBlobSummary.format) << "\",\n";
    stream << "      \"sectorSize\": " << discBlobSummary.sectorSize << ",\n";
    stream << "      \"lbaCount\": " << discBlobSummary.lbaCount << ",\n";
    stream << "      \"bytes\": " << discBlobSummary.blobBytes << ",\n";
    stream << "      \"sha1\": \"" << escapeJson(discBlobSummary.blobSha1) << "\",\n";
    stream << "      \"blobPath\": \"" << escapeJson(discBlobSummary.blobPath) << "\",\n";
    stream << "      \"layoutPath\": \"" << escapeJson(discBlobSummary.layoutPath) << "\",\n";
    stream << "      \"hashPath\": \"" << escapeJson(discBlobSummary.hashesPath) << "\",\n";
    stream << "      \"hashesPath\": \"" << escapeJson(discBlobSummary.hashesPath) << "\",\n";
    stream << "      \"caps\": {\n";
    stream << "        \"maxBytes\": " << discBlobSummary.maxBytes << "\n";
    stream << "      },\n";
    stream << "      \"result\": {\n";
    stream << "        \"lbaStart\": " << discBlobSummary.lbaStart << ",\n";
    stream << "        \"lbaCount\": " << discBlobSummary.lbaCount << ",\n";
    stream << "        \"blobBytes\": " << discBlobSummary.blobBytes << ",\n";
    stream << "        \"truncated\": " << (discBlobSummary.truncated ? "true" : "false") << ",\n";
    stream << "        \"sha1\": \"" << escapeJson(discBlobSummary.blobSha1) << "\"\n";
    stream << "      }\n";
    stream << "    },\n";
    stream << "    \"embeddedScan\": {\n";
    stream << "      \"enabled\": " << (embeddedScanSummary.enabled ? "true" : "false") << ",\n";
    stream << "      \"result\": {\n";
    stream << "        \"containersScanned\": " << embeddedScanSummary.containersScanned << ",\n";
    stream << "        \"hitsExtracted\": " << embeddedScanSummary.hitsExtracted << "\n";
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
    stream << "    \"discBlobEnabled\": " << (discBlobSummary.enabled ? "true" : "false") << ",\n";
    stream << "    \"discBlobSectorSize\": " << discBlobSummary.sectorSize << ",\n";
    stream << "    \"discBlobPath\": \"" << escapeJson(discBlobSummary.blobPath) << "\",\n";
    stream << "    \"discBlobHashPath\": \"" << escapeJson(discBlobSummary.hashesPath) << "\",\n";
    stream << "    \"discBlobBytes\": " << discBlobSummary.blobBytes << ",\n";
    stream << "    \"discBlobLbaCount\": " << discBlobSummary.lbaCount << ",\n";
    stream << "    \"containersScanned\": " << embeddedScanSummary.containersScanned << ",\n";
    stream << "    \"hitsExtracted\": " << embeddedScanSummary.hitsExtracted << "\n";
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

std::string serializeCatalog(const std::vector<CatalogEntry>& entries)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schemaVersion\": \"1.0\",\n";
    stream << "  \"entries\": [\n";
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        stream << "    {\n";
        stream << "      \"id\": \"" << escapeJson(entry.id) << "\",\n";
        stream << "      \"sourceKind\": \"" << escapeJson(entry.sourceKind) << "\",\n";
        stream << "      \"isoPath\": \"" << escapeJson(entry.isoPath) << "\",\n";
        stream << "      \"exportedPath\": \"" << escapeJson(entry.exportedPath) << "\",\n";
        stream << "      \"size\": " << entry.size << ",\n";
        stream << "      \"hashes\": {\n";
        stream << "        \"sha1\": \"" << escapeJson(entry.sha1) << "\"\n";
        stream << "      },\n";
        stream << "      \"source\": {\n";
        stream << "        \"containerIsoPath\": \"" << escapeJson(entry.containerIsoPath)
               << "\",\n";
        stream << "        \"offset\": " << entry.containerOffset << "\n";
        stream << "      },\n";
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
        stream << "      ],\n";
        stream << "      \"detectedTypes\": [\n";
        for (size_t typeIndex = 0; typeIndex < entry.detectedTypes.size(); ++typeIndex)
        {
            stream << "        \"" << escapeJson(entry.detectedTypes[typeIndex]) << "\"";
            if (typeIndex + 1 < entry.detectedTypes.size())
            {
                stream << ",";
            }
            stream << "\n";
        }
        stream << "      ]\n";
        stream << "    }";
        if (index + 1 < entries.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ]\n";
    stream << "}\n";
    return stream.str();
}

std::string serializeRecompInputs(const std::string& bootIsoPath,
                                  const std::string& bootExportedPath,
                                  const std::string& systemCnfExportedPath,
                                  const std::vector<RecompInputExecutable>& executables)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schemaVersion\": \"1.0\",\n";
    stream << "  \"boot\": {\n";
    stream << "    \"isoPath\": \"" << escapeJson(bootIsoPath) << "\",\n";
    stream << "    \"exportedPath\": \"" << escapeJson(bootExportedPath) << "\"\n";
    stream << "  },\n";
    stream << "  \"systemCnf\": {\n";
    stream << "    \"exportedPath\": \"" << escapeJson(systemCnfExportedPath) << "\"\n";
    stream << "  },\n";
    stream << "  \"executables\": [\n";
    for (size_t index = 0; index < executables.size(); ++index)
    {
        const auto& executable = executables[index];
        stream << "    {\n";
        stream << "      \"isoPath\": \"" << escapeJson(executable.isoPath) << "\",\n";
        stream << "      \"exportedPath\": \"" << escapeJson(executable.exportedPath) << "\",\n";
        stream << "      \"psxExe\": {\n";
        stream << "        \"loadAddr\": \"0x" << formatHex(executable.loadAddress, 8) << "\",\n";
        stream << "        \"entry\": \"0x" << formatHex(executable.entryPoint, 8) << "\",\n";
        stream << "        \"size\": " << executable.loadSize;
        if (executable.gp != 0)
        {
            stream << ",\n";
            stream << "        \"gp\": \"0x" << formatHex(executable.gp, 8) << "\"";
        }
        if (executable.bssSize != 0)
        {
            stream << ",\n";
            stream << "        \"bssAddr\": \"0x" << formatHex(executable.bssAddress, 8) << "\",\n";
            stream << "        \"bssSize\": " << executable.bssSize;
        }
        if (executable.stackSize != 0 || executable.stackAddress != 0)
        {
            stream << ",\n";
            stream << "        \"stackAddr\": \"0x" << formatHex(executable.stackAddress, 8)
                   << "\",\n";
            stream << "        \"stackSize\": " << executable.stackSize;
        }
        stream << "\n";
        stream << "      }\n";
        stream << "    }";
        if (index + 1 < executables.size())
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
