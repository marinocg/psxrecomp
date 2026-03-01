#include "psxrecomp/iso/iso_parser.h"

#include "iso_sector.h"
#include "iso_utils.h"
#include "psxrecomp/iso/iso_boot.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace iso
{

namespace
{

constexpr u32 kUserDataSize = detail::kUserDataSize;

bool isIsoRelativePath(const std::string& path)
{
    if (path.empty())
    {
        return false;
    }

    std::filesystem::path fsPath(path);
    if (fsPath.is_absolute())
    {
        return false;
    }

    const std::vector<std::string> components = detail::splitPath(path);
    if (components.empty())
    {
        return false;
    }
    for (const auto& component : components)
    {
        if (component.empty() || component == "." || component == "..")
        {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<u8> IsoParser::extractFile(const std::string& path)
{
    if (!m_isOpen)
    {
        return {};
    }

    DirectoryRecord target{};
    std::vector<DirectoryRecord> targetExtents;
    if (!findFileExtents(path, target, targetExtents))
    {
        return {};
    }

    std::vector<u8> data;
    std::sort(targetExtents.begin(), targetExtents.end(),
              [](const DirectoryRecord& lhs, const DirectoryRecord& rhs)
              { return lhs.extentLocation < rhs.extentLocation; });

    u32 totalLength = 0;
    if (targetExtents.size() == 1)
    {
        totalLength = targetExtents.front().dataLength;
    }
    else
    {
        bool hasMultiExtent = false;
        for (const auto& extent : targetExtents)
        {
            hasMultiExtent = hasMultiExtent || ((extent.flags & 0x80) != 0);
            totalLength += extent.dataLength;
        }
        if (!hasMultiExtent && !targetExtents.empty())
        {
            totalLength = targetExtents.front().dataLength;
            targetExtents = {targetExtents.front()};
        }
    }
    data.reserve(totalLength);

    for (const auto& extent : targetExtents)
    {
        u32 remaining = extent.dataLength;
        u32 sector = extent.extentLocation;
        while (remaining > 0)
        {
            auto sectorData = readSector(sector);
            if (sectorData.empty())
            {
                return {};
            }
            u32 toCopy = std::min<u32>(remaining, static_cast<u32>(sectorData.size()));
            data.insert(data.end(), sectorData.begin(), sectorData.begin() + toCopy);
            remaining -= toCopy;
            ++sector;
        }
    }

    return data;
}

bool IsoParser::exportFileTo(const std::string& isoPath, const std::filesystem::path& destination,
                             std::string* outError)
{
    std::ofstream out;
    bool wroteTempFile = false;
    std::filesystem::path tempDestination;
    auto cleanupTemp = [&]()
    {
        if (out.is_open())
        {
            out.close();
        }
        if (wroteTempFile)
        {
            std::error_code cleanupError;
            std::filesystem::remove(tempDestination, cleanupError);
        }
    };

    auto setError = [&](const std::string& message)
    {
        cleanupTemp();
        addError(message);
        if (outError)
        {
            *outError = message;
        }
        return false;
    };

    if (!m_isOpen)
    {
        return setError("ISO parser is not open.");
    }

    if (!isIsoRelativePath(isoPath))
    {
        return setError("ISO path must be relative and must not contain traversal segments.");
    }

    if (destination.empty())
    {
        return setError("Destination path is empty.");
    }

    DirectoryRecord target{};
    std::vector<DirectoryRecord> targetExtents;
    if (!findFileExtents(isoPath, target, targetExtents))
    {
        return setError("Failed to locate ISO file: " + isoPath);
    }

    std::sort(targetExtents.begin(), targetExtents.end(),
              [](const DirectoryRecord& lhs, const DirectoryRecord& rhs)
              { return lhs.extentLocation < rhs.extentLocation; });

    u32 totalLength = 0;
    if (targetExtents.size() == 1)
    {
        totalLength = targetExtents.front().dataLength;
    }
    else
    {
        bool hasMultiExtent = false;
        for (const auto& extent : targetExtents)
        {
            hasMultiExtent = hasMultiExtent || ((extent.flags & 0x80) != 0);
            totalLength += extent.dataLength;
        }
        if (!hasMultiExtent && !targetExtents.empty())
        {
            totalLength = targetExtents.front().dataLength;
            targetExtents = {targetExtents.front()};
        }
    }

    if (outError)
    {
        outError->clear();
    }

    std::error_code fsError;
    const std::filesystem::path parent = destination.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, fsError);
        if (fsError)
        {
            return setError("Failed to create destination directory: " + parent.string() + ": " +
                            fsError.message());
        }
    }

    tempDestination = destination;
    tempDestination +=
        ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    out.open(tempDestination, std::ios::binary);
    if (!out)
    {
        return setError("Failed to open temporary destination file: " + tempDestination.string());
    }
    wroteTempFile = true;

    u32 remainingTotal = totalLength;
    for (const auto& extent : targetExtents)
    {
        u32 remainingExtent = extent.dataLength;
        u32 sector = extent.extentLocation;
        while (remainingExtent > 0 && remainingTotal > 0)
        {
            const auto sectorData = readSector(sector);
            if (sectorData.empty())
            {
                return setError("Failed to read ISO sector while exporting: " + isoPath);
            }

            const u32 toWrite = std::min<u32>(
                {remainingExtent, remainingTotal, static_cast<u32>(sectorData.size())});
            out.write(reinterpret_cast<const char*>(sectorData.data()),
                      static_cast<std::streamsize>(toWrite));
            if (!out.good())
            {
                return setError("Failed to write destination file: " + destination.string());
            }

            remainingExtent -= toWrite;
            remainingTotal -= toWrite;
            ++sector;
        }
    }

    out.flush();
    if (!out.good())
    {
        return setError("Failed to flush destination file: " + destination.string());
    }

    out.close();
    if (!out.good())
    {
        return setError("Failed to close temporary destination file: " + tempDestination.string());
    }

    std::filesystem::rename(tempDestination, destination, fsError);
    if (fsError)
    {
        return setError("Failed to finalize destination file: " + destination.string() + ": " +
                        fsError.message());
    }
    wroteTempFile = false;

    return true;
}

std::string IsoParser::findExecutable()
{
    if (!m_isOpen)
    {
        return "";
    }

    auto systemCnf = extractFile("SYSTEM.CNF");
    auto bootPath = detail::parseBootPathFromSystemCnf(systemCnf);
    if (!bootPath.empty())
    {
        return bootPath;
    }

    auto executables = listExecutables();
    if (!executables.empty())
    {
        return executables.front();
    }

    return "";
}

std::string IsoParser::getVolumeLabel() const
{
    std::string label(m_pvd.volumeId, m_pvd.volumeId + sizeof(m_pvd.volumeId));
    auto nullPos = label.find('\0');
    if (nullPos != std::string::npos)
    {
        label.erase(nullPos);
    }
    return detail::trimSpaces(label);
}

u32 IsoParser::getLogicalBlockSize() const
{
    return m_logicalBlockSize;
}

u32 IsoParser::getRawSectorSize() const
{
    return m_rawSectorSize;
}

bool IsoParser::isUsingJoliet() const
{
    return m_useJoliet;
}

u32 IsoParser::getTotalSectors() const
{
    return m_totalSectors;
}

const std::vector<TrackInfo>& IsoParser::getTracks() const
{
    return m_tracks;
}

std::optional<TrackInfo> IsoParser::getDataTrack() const
{
    const auto* track = selectPrimaryDataTrack();
    if (!track)
    {
        return std::nullopt;
    }
    return *track;
}

const std::vector<std::string>& IsoParser::getErrors() const
{
    return m_errors;
}

std::string IsoParser::getLastError() const
{
    if (m_errors.empty())
    {
        return "";
    }
    return m_errors.back();
}

bool IsoParser::isValid() const
{
    return m_isValid;
}

std::vector<std::string> IsoParser::listExecutables()
{
    return listFilesByExtension({".EXE"}, false);
}

std::vector<IsoFileEntry> IsoParser::listAllFilesRecursive()
{
    std::vector<IsoFileEntry> allEntries;
    if (!m_isOpen)
    {
        return allEntries;
    }

    struct PendingDirectory
    {
        std::string path;
        u32 extent = 0;
        u32 size = 0;
    };

    struct FileVersionRecords
    {
        int bestVersion = -1;
        std::vector<DirectoryRecord> records;
    };

    auto directoryKey = [](u32 extent, u32 size) -> u64
    { return (static_cast<u64>(extent) << 32) | static_cast<u64>(size); };

    std::deque<PendingDirectory> pendingDirectories;
    pendingDirectories.push_back(PendingDirectory{"", m_rootExtent, m_rootSize});

    std::unordered_set<u64> visitedDirectories;
    std::unordered_map<std::string, IsoFileEntry> directoriesByPath;
    std::unordered_map<std::string, FileVersionRecords> filesByPath;

    while (!pendingDirectories.empty())
    {
        PendingDirectory current = pendingDirectories.front();
        pendingDirectories.pop_front();

        if (current.size == 0)
        {
            continue;
        }

        if (!visitedDirectories.insert(directoryKey(current.extent, current.size)).second)
        {
            continue;
        }

        std::vector<DirectoryRecord> records;
        if (!readDirectory(current.extent, current.size, records))
        {
            continue;
        }

        for (const auto& record : records)
        {
            if (record.name.empty() || record.name == "." || record.name == "..")
            {
                continue;
            }

            const bool isDirectory = (record.flags & 0x02) != 0;
            const std::string normalizedName =
                detail::normalizeIsoName(detail::baseIsoName(record.name));
            if (normalizedName.empty())
            {
                continue;
            }

            std::string fullPath = current.path;
            if (!fullPath.empty())
            {
                fullPath += "/";
            }
            fullPath += normalizedName;

            if (isDirectory)
            {
                auto [it, inserted] = directoriesByPath.emplace(fullPath, IsoFileEntry{});
                if (inserted)
                {
                    it->second.path = fullPath;
                    it->second.size = record.dataLength;
                    it->second.flags = record.flags;
                    it->second.isDirectory = true;
                    it->second.extents.push_back(
                        IsoFileExtent{record.extentLocation, record.dataLength, false});
                }
                if (record.extentLocation != 0 && record.dataLength != 0)
                {
                    pendingDirectories.push_back(
                        PendingDirectory{fullPath, record.extentLocation, record.dataLength});
                }
                continue;
            }

            const int version = detail::isoVersionNumber(record.name);
            auto& versionRecords = filesByPath[fullPath];
            if (version > versionRecords.bestVersion)
            {
                versionRecords.bestVersion = version;
                versionRecords.records.clear();
            }
            if (version == versionRecords.bestVersion)
            {
                versionRecords.records.push_back(record);
            }
        }
    }

    allEntries.reserve(directoriesByPath.size() + filesByPath.size());
    for (const auto& [path, entry] : directoriesByPath)
    {
        (void)path;
        allEntries.push_back(entry);
    }

    for (auto& [path, versionRecords] : filesByPath)
    {
        (void)path;
        auto& records = versionRecords.records;
        if (records.empty())
        {
            continue;
        }

        std::sort(records.begin(), records.end(),
                  [](const DirectoryRecord& lhs, const DirectoryRecord& rhs)
                  { return lhs.extentLocation < rhs.extentLocation; });

        const bool hasMultiExtent =
            std::any_of(records.begin(), records.end(),
                        [](const DirectoryRecord& record) { return (record.flags & 0x80) != 0; });

        IsoFileEntry entry;
        entry.path = path;
        entry.isDirectory = false;

        if (hasMultiExtent || records.size() == 1)
        {
            for (const auto& record : records)
            {
                entry.extents.push_back(IsoFileExtent{record.extentLocation, record.dataLength,
                                                      (record.flags & 0x80) != 0});
                entry.size += record.dataLength;
                entry.flags |= record.flags;
            }
        }
        else
        {
            const auto& record = records.front();
            entry.size = record.dataLength;
            entry.flags = record.flags;
            entry.extents.push_back(IsoFileExtent{record.extentLocation, record.dataLength, false});
        }

        allEntries.push_back(std::move(entry));
    }

    std::sort(allEntries.begin(), allEntries.end(),
              [](const IsoFileEntry& lhs, const IsoFileEntry& rhs)
              {
                  if (lhs.path == rhs.path)
                  {
                      return lhs.isDirectory && !rhs.isDirectory;
                  }
                  return lhs.path < rhs.path;
              });

    return allEntries;
}

} // namespace iso
} // namespace psxrecomp
