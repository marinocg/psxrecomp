#include "psxrecomp/iso/iso_parser.h"

#include "iso_sector.h"
#include "iso_utils.h"
#include "psxrecomp/iso/iso_boot.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace iso
{

namespace
{

constexpr u32 kUserDataSize = detail::kUserDataSize;

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

bool IsoParser::readDirectory(u32 extent, u32 size, std::vector<DirectoryRecord>& records)
{
    if (size == 0)
    {
        addError("Directory size is zero.");
        return false;
    }

    records.clear();
    u32 remaining = size;
    u32 sector = extent;
    std::vector<u8> buffer;
    buffer.reserve(size);

    while (remaining > 0)
    {
        auto data = readSector(sector);
        if (data.empty())
        {
            addError("Failed to read directory sector.");
            return false;
        }
        u32 toCopy = std::min<u32>(remaining, static_cast<u32>(data.size()));
        buffer.insert(buffer.end(), data.begin(), data.begin() + toCopy);
        remaining -= toCopy;
        ++sector;
    }

    size_t offset = 0;
    while (offset < buffer.size())
    {
        u8 length = buffer[offset];
        if (length == 0)
        {
            size_t blockSize =
                m_logicalBlockSize != 0 ? static_cast<size_t>(m_logicalBlockSize) : kUserDataSize;
            size_t nextBoundary = ((offset / blockSize) + 1) * blockSize;
            if (nextBoundary <= offset)
            {
                break;
            }
            offset = std::min(nextBoundary, buffer.size());
            continue;
        }
        if (offset + length > buffer.size())
        {
            addError("Directory record exceeds buffer length.");
            return false;
        }

        const u8* recordData = buffer.data() + offset;
        DirectoryRecord record{};
        record.length = recordData[0];
        record.extendedLength = recordData[1];
        record.extentLocation = detail::readLe32(recordData + 2);
        record.dataLength = detail::readLe32(recordData + 10);
        std::memcpy(record.recordingDateTime, recordData + 18, 7);
        record.flags = recordData[25];
        record.fileUnitSize = recordData[26];
        record.interleaveGapSize = recordData[27];
        record.volumeSequenceNumber = detail::readLe16(recordData + 28);
        record.nameLength = recordData[32];
        if (record.nameLength > 0 && 33 + record.nameLength <= length)
        {
            if (m_useJoliet)
            {
                record.name = detail::decodeJolietName(recordData + 33, record.nameLength);
            }
            else
            {
                record.name.assign(reinterpret_cast<const char*>(recordData + 33),
                                   record.nameLength);
            }
            if (record.nameLength == 1)
            {
                if (record.name[0] == '\0')
                {
                    record.name = ".";
                }
                else if (static_cast<unsigned char>(record.name[0]) == 1)
                {
                    record.name = "..";
                }
            }
        }
        records.push_back(record);
        offset += length;
    }

    return true;
}

bool IsoParser::getDirectoryInfo(const std::string& path, DirectoryInfo& info)
{
    if (path.empty())
    {
        info.extent = m_rootExtent;
        info.size = m_rootSize;
        return true;
    }

    auto cacheIt = m_directoryCache.find(path);
    if (cacheIt != m_directoryCache.end())
    {
        info = cacheIt->second;
        return true;
    }

    auto extentIt = m_pathTable.find(path);
    if (extentIt == m_pathTable.end())
    {
        return false;
    }

    u32 size = 0;
    if (!readDirectorySelfSize(extentIt->second, size))
    {
        return false;
    }

    info.extent = extentIt->second;
    info.size = size;
    m_directoryCache[path] = info;
    return true;
}

bool IsoParser::readDirectorySelfSize(u32 extent, u32& outSize)
{
    auto data = readSector(extent);
    if (data.empty())
    {
        return false;
    }
    if (data[0] == 0)
    {
        addError("Directory record missing self entry.");
        return false;
    }
    u8 length = data[0];
    if (length < 34 || length > data.size())
    {
        addError("Directory record length is invalid.");
        return false;
    }
    outSize = detail::readLe32(data.data() + 10);
    return outSize != 0;
}

bool IsoParser::findFileExtents(const std::string& path, DirectoryRecord& target,
                                std::vector<DirectoryRecord>& extents)
{
    std::vector<std::string> components = detail::splitPath(path);
    if (components.empty())
    {
        return false;
    }

    std::vector<DirectoryRecord> records;
    if (!readDirectory(m_rootExtent, m_rootSize, records))
    {
        return false;
    }

    std::string currentPath;
    for (size_t i = 0; i < components.size(); ++i)
    {
        std::string desired = detail::normalizeIsoName(components[i]);
        bool isLast = i == components.size() - 1;
        std::string nextPath = currentPath;
        if (!desired.empty())
        {
            if (!nextPath.empty())
            {
                nextPath += "/";
            }
            nextPath += desired;
        }

        if (!isLast && !m_pathTable.empty())
        {
            DirectoryInfo info{};
            if (getDirectoryInfo(nextPath, info))
            {
                currentPath = nextPath;
                if (!readDirectory(info.extent, info.size, records))
                {
                    return false;
                }
                continue;
            }
        }

        bool found = false;
        extents.clear();
        std::string desiredBase = detail::normalizeIsoName(detail::baseIsoName(components[i]));
        bool desiredHasVersion = components[i].find(';') != std::string::npos;
        int bestVersion = -1;
        DirectoryRecord bestRecord{};
        for (const auto& record : records)
        {
            if (record.name.empty())
            {
                continue;
            }
            if (record.name == "." || record.name == "..")
            {
                continue;
            }
            auto recordName = detail::normalizeIsoName(record.name);
            if (recordName == desired)
            {
                found = true;
                target = record;
                extents.push_back(record);
                continue;
            }
            if (!desiredHasVersion &&
                detail::normalizeIsoName(detail::baseIsoName(record.name)) == desiredBase)
            {
                int version = detail::isoVersionNumber(record.name);
                if (version > bestVersion)
                {
                    bestVersion = version;
                    bestRecord = record;
                }
            }
        }

        if (!found && bestVersion >= 0)
        {
            found = true;
            target = bestRecord;
            extents.clear();
            for (const auto& record : records)
            {
                if (record.name.empty())
                {
                    continue;
                }
                if (record.name == "." || record.name == "..")
                {
                    continue;
                }
                if (detail::normalizeIsoName(detail::baseIsoName(record.name)) == desiredBase &&
                    detail::isoVersionNumber(record.name) == bestVersion)
                {
                    extents.push_back(record);
                }
            }
            if (extents.empty())
            {
                extents.push_back(bestRecord);
            }
        }

        if (!found)
        {
            addError("Failed to locate ISO file: " + desired);
            return false;
        }

        if (!isLast)
        {
            if ((target.flags & 0x02) == 0)
            {
                return false;
            }
            records.clear();
            if (!readDirectory(target.extentLocation, target.dataLength, records))
            {
                return false;
            }
            currentPath = nextPath;
        }
    }

    if ((target.flags & 0x02) != 0)
    {
        return false;
    }

    return true;
}

const TrackInfo* IsoParser::selectPrimaryDataTrack() const
{
    const TrackInfo* selected = nullptr;
    for (const auto& track : m_tracks)
    {
        if (track.type != TrackType::Data)
        {
            continue;
        }
        if (!selected)
        {
            selected = &track;
            continue;
        }
        if (track.sessionNumber > selected->sessionNumber)
        {
            selected = &track;
            continue;
        }
        if (track.sessionNumber == selected->sessionNumber && track.startLba > selected->startLba)
        {
            selected = &track;
        }
    }
    return selected;
}

} // namespace iso
} // namespace psxrecomp
