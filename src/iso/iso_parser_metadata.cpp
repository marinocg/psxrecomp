#include "psxrecomp/iso/iso_parser.h"

#include "iso_utils.h"
#include "psxrecomp/iso/iso_boot.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace iso
{
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

u32 IsoParser::getVolumeSpaceSize() const
{
    return m_pvd.volumeSpaceSize;
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
