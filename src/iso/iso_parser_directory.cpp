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
