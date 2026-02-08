#include "psxrecomp/iso/iso_parser.h"

#include "cue_sheet.h"
#include "iso_utils.h"
#include "path_table.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace psxrecomp
{
namespace iso
{

namespace
{

constexpr u32 kUserDataSize = 2048;
constexpr u32 kRawSectorSize = 2352;
constexpr u8 kMode1 = 1;
constexpr u8 kMode2 = 2;
constexpr u8 kSubmodeForm2 = 0x20;

struct SectorView
{
    size_t offset = 0;
    size_t size = 0;
};

SectorView decodeSectorLayout(const std::vector<u8>& raw)
{
    if (raw.size() == kUserDataSize)
    {
        return {0, kUserDataSize};
    }
    if (raw.size() < kRawSectorSize)
    {
        return {0, 0};
    }
    u8 mode = raw[15];
    if (mode == kMode1)
    {
        return {16, kUserDataSize};
    }
    if (mode == kMode2)
    {
        u8 submode = raw[18];
        if ((submode & kSubmodeForm2) != 0)
        {
            return {24, 2324};
        }
        return {24, kUserDataSize};
    }
    return {0, 0};
}

std::string parseBootPathFromSystemCnf(const std::vector<u8>& systemCnf)
{
    if (systemCnf.empty())
    {
        return "";
    }

    std::string contents(systemCnf.begin(), systemCnf.end());
    std::istringstream stream(contents);
    std::string line;
    while (std::getline(stream, line))
    {
        auto upper = detail::toUpper(line);
        auto pos = upper.find("BOOT");
        if (pos == std::string::npos)
        {
            continue;
        }
        auto equals = upper.find('=', pos);
        if (equals == std::string::npos)
        {
            continue;
        }
        std::string value = line.substr(equals + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        auto upperValue = detail::toUpper(value);
        auto prefixPos = upperValue.find("CDROM");
        if (prefixPos != std::string::npos)
        {
            auto colonPos = upperValue.find(':', prefixPos);
            if (colonPos != std::string::npos)
            {
                value = value.substr(colonPos + 1);
            }
        }
        while (!value.empty() && (value[0] == '\\' || value[0] == '/'))
        {
            value.erase(value.begin());
        }
        std::replace(value.begin(), value.end(), '\\', '/');
        return detail::normalizeIsoName(value);
    }

    return "";
}

} // namespace

IsoParser::IsoParser(const std::string& filename)
    : m_filename(filename), m_isOpen(false), m_isValid(false), m_rawSectorSize(kUserDataSize),
      m_dataTrackStartLba(0), m_logicalBlockSize(kUserDataSize), m_useJoliet(false), m_stream(),
      m_rawSectorScratch(), m_pvd{}, m_rootDirectory(), m_rootExtent(0), m_rootSize(0),
      m_totalSectors(0), m_tracks(), m_errors()
{
}

IsoParser::~IsoParser()
{
    if (m_stream.is_open())
    {
        m_stream.close();
    }
}

bool IsoParser::open()
{
    m_errors.clear();
    m_tracks.clear();
    m_directoryCache.clear();
    m_pathTable.clear();
    m_totalSectors = 0;

    if (!openStream())
    {
        return false;
    }

    m_isOpen = readPVD();
    m_isValid = m_isOpen;
    return m_isOpen;
}

std::vector<u8> IsoParser::extractFile(const std::string& path)
{
    if (!m_isOpen)
    {
        return {};
    }

    std::vector<std::string> components = detail::splitPath(path);
    if (components.empty())
    {
        return {};
    }

    std::vector<DirectoryRecord> records;
    if (!readDirectory(m_rootExtent, m_rootSize, records))
    {
        return {};
    }

    DirectoryRecord target{};
    std::vector<DirectoryRecord> targetExtents;
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
                    return {};
                }
                continue;
            }
        }

        bool found = false;
        targetExtents.clear();
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
                targetExtents.push_back(record);
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
            targetExtents.clear();
            targetExtents.push_back(bestRecord);
        }

        if (!found)
        {
            addError("Failed to locate ISO file: " + desired);
            return {};
        }

        if (!isLast)
        {
            if ((target.flags & 0x02) == 0)
            {
                return {};
            }
            records.clear();
            if (!readDirectory(target.extentLocation, target.dataLength, records))
            {
                return {};
            }
            currentPath = nextPath;
        }
    }

    if ((target.flags & 0x02) != 0)
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
    auto bootPath = parseBootPathFromSystemCnf(systemCnf);
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

const std::vector<TrackInfo>& IsoParser::getTracks() const
{
    return m_tracks;
}

std::optional<TrackInfo> IsoParser::getDataTrack() const
{
    for (const auto& track : m_tracks)
    {
        if (track.type == TrackType::Data)
        {
            return track;
        }
    }
    return std::nullopt;
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
    std::vector<std::string> executables;
    if (!m_isOpen)
    {
        return executables;
    }

    std::vector<std::pair<std::string, DirectoryInfo>> directories;
    directories.emplace_back("", DirectoryInfo{m_rootExtent, m_rootSize});

    if (!m_pathTable.empty())
    {
        directories.clear();
        directories.reserve(m_pathTable.size());
        for (const auto& entry : m_pathTable)
        {
            DirectoryInfo info{};
            if (!getDirectoryInfo(entry.first, info))
            {
                continue;
            }
            directories.emplace_back(entry.first, info);
        }
    }

    for (const auto& entry : directories)
    {
        std::vector<DirectoryRecord> records;
        if (!readDirectory(entry.second.extent, entry.second.size, records))
        {
            continue;
        }
        for (const auto& record : records)
        {
            if ((record.flags & 0x02) != 0)
            {
                continue;
            }
            auto name = detail::normalizeIsoName(record.name);
            if (name.size() >= 4 && name.substr(name.size() - 4) == ".EXE")
            {
                std::string path = entry.first;
                if (!path.empty())
                {
                    path += "/";
                }
                path += name;
                executables.push_back(path);
            }
        }
    }

    std::sort(executables.begin(), executables.end());
    executables.erase(std::unique(executables.begin(), executables.end()), executables.end());
    return executables;
}

bool IsoParser::readPVD()
{
    std::array<u8, kUserDataSize> sector{};
    std::optional<PrimaryVolumeDescriptor> pvd;
    std::optional<std::pair<u32, u32>> pvdRoot;
    std::optional<std::pair<u32, u32>> jolietRoot;

    u32 trackStart = m_dataTrackStartLba;
    m_useJoliet = false;

    std::array<u32, 2> layoutSizes = {
        m_rawSectorSize, m_rawSectorSize == kUserDataSize ? kRawSectorSize : kUserDataSize};
    for (u32 layoutSize : layoutSizes)
    {
        m_rawSectorSize = layoutSize;
        m_dataTrackStartLba = trackStart;
        if (!readSectorInto(16, sector.data(), sector.size()))
        {
            continue;
        }

        for (u32 descriptorIndex = 16; descriptorIndex < 32; ++descriptorIndex)
        {
            if (!readSectorInto(descriptorIndex, sector.data(), sector.size()))
            {
                break;
            }
            u8 type = sector[0];
            if (std::memcmp(sector.data() + 1, "CD001", 5) != 0)
            {
                continue;
            }
            if (type == 255)
            {
                break;
            }
            if (type == 1)
            {
                PrimaryVolumeDescriptor candidate{};
                std::memcpy(candidate.identifier, sector.data() + 1, 5);
                candidate.type = sector[0];
                candidate.version = sector[6];
                std::memcpy(candidate.systemId, sector.data() + 8, 32);
                std::memcpy(candidate.volumeId, sector.data() + 40, 32);
                candidate.volumeSpaceSize = detail::readLe32(sector.data() + 80);
                candidate.volumeSetSize = detail::readLe16(sector.data() + 120);
                candidate.volumeSequenceNumber = detail::readLe16(sector.data() + 124);
                candidate.logicalBlockSize = detail::readLe16(sector.data() + 128);
                candidate.pathTableSize = detail::readLe32(sector.data() + 132);
                candidate.pathTableLba = detail::readLe32(sector.data() + 140);
                candidate.optionalPathTableLba = detail::readLe32(sector.data() + 148);
                if (!validateVolumeMetadata(candidate))
                {
                    continue;
                }
                pvd = candidate;
                const u8* rootRecord = sector.data() + 156;
                pvdRoot = std::make_pair(detail::readLe32(rootRecord + 2),
                                         detail::readLe32(rootRecord + 10));
            }
            if (type == 2)
            {
                if (sector[88] == 0x25 && sector[89] == 0x2F &&
                    (sector[90] == 0x40 || sector[90] == 0x43 || sector[90] == 0x45))
                {
                    const u8* rootRecord = sector.data() + 156;
                    u32 rootExtent = detail::readLe32(rootRecord + 2);
                    u32 rootSize = detail::readLe32(rootRecord + 10);
                    jolietRoot = std::make_pair(rootExtent, rootSize);
                }
            }
        }

        if (pvd)
        {
            m_pvd = *pvd;
            m_logicalBlockSize = m_pvd.logicalBlockSize;
            if (jolietRoot)
            {
                m_useJoliet = true;
                m_rootExtent = jolietRoot->first;
                m_rootSize = jolietRoot->second;
            }
            else if (pvdRoot)
            {
                m_useJoliet = false;
                m_rootExtent = pvdRoot->first;
                m_rootSize = pvdRoot->second;
            }
            if (!loadPathTable())
            {
                addError("Failed to parse ISO path table.");
            }
            m_rootDirectory.clear();
            if (!readDirectory(m_rootExtent, m_rootSize, m_rootDirectory))
            {
                return false;
            }
            return true;
        }
    }

    return false;
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
            break;
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

std::vector<u8> IsoParser::readSector(u32 sector)
{
    std::vector<u8> raw(m_rawSectorSize);
    if (!readRawSector(sector, raw))
    {
        addError("Failed to read raw sector.");
        return {};
    }
    if (m_rawSectorSize == kUserDataSize)
    {
        return raw;
    }
    auto view = decodeSectorLayout(raw);
    if (view.size == 0 || view.offset + view.size > raw.size())
    {
        addError("Unsupported sector layout.");
        return {};
    }
    return std::vector<u8>(raw.begin() + static_cast<std::ptrdiff_t>(view.offset),
                           raw.begin() + static_cast<std::ptrdiff_t>(view.offset + view.size));
}

bool IsoParser::readRawSector(u32 sector, std::vector<u8>& buffer)
{
    if (!m_stream)
    {
        addError("Stream not open for sector read.");
        return false;
    }
    m_stream.clear();
    if (buffer.size() != m_rawSectorSize)
    {
        buffer.resize(m_rawSectorSize);
    }
    std::streamoff offset = static_cast<std::streamoff>(sector + m_dataTrackStartLba) *
                            static_cast<std::streamoff>(m_rawSectorSize);
    m_stream.seekg(offset, std::ios::beg);
    if (!m_stream.good())
    {
        addError("Failed to seek to sector offset.");
        return false;
    }
    m_stream.read(reinterpret_cast<char*>(buffer.data()),
                  static_cast<std::streamsize>(buffer.size()));
    if (m_stream.gcount() != static_cast<std::streamsize>(buffer.size()))
    {
        addError("Short read while reading sector.");
        return false;
    }
    return true;
}

bool IsoParser::readSectorInto(u32 sector, u8* buffer, size_t size)
{
    if (!m_stream)
    {
        addError("Stream not open for sector read.");
        return false;
    }
    if (m_rawSectorSize == kUserDataSize)
    {
        if (!readRawSector(sector, m_rawSectorScratch))
        {
            return false;
        }
        if (size > m_rawSectorScratch.size())
        {
            addError("Requested sector slice exceeds buffer size.");
            return false;
        }
        std::memcpy(buffer, m_rawSectorScratch.data(), size);
        return true;
    }

    if (!readRawSector(sector, m_rawSectorScratch))
    {
        return false;
    }
    auto view = decodeSectorLayout(m_rawSectorScratch);
    if (view.size < size || view.offset + size > m_rawSectorScratch.size())
    {
        addError("Invalid sector view.");
        return false;
    }
    std::memcpy(buffer, m_rawSectorScratch.data() + view.offset, size);
    return true;
}

bool IsoParser::openStream()
{
    if (m_stream.is_open())
    {
        m_stream.close();
    }

    CueSheet cueSheet{};
    std::string cueError;
    std::filesystem::path inputPath(m_filename);
    bool isCue =
        inputPath.has_extension() && detail::toUpper(inputPath.extension().string()) == ".CUE";

    if (isCue)
    {
        if (!parseCueSheet(m_filename, cueSheet, cueError))
        {
            addError(cueError.empty() ? "Failed to parse CUE sheet." : cueError);
            return false;
        }

        m_tracks = cueSheet.tracks;
        const auto* dataTrack = cueSheet.findFirstDataTrack();
        if (!dataTrack)
        {
            addError("No data track found in CUE sheet.");
            return false;
        }

        for (auto& track : m_tracks)
        {
            if (!track.file.empty())
            {
                std::filesystem::path resolved = inputPath.parent_path() / track.file;
                track.file = resolved.string();
            }
        }

        std::filesystem::path binPath = inputPath.parent_path() / dataTrack->file;
        m_filename = binPath.string();
        m_rawSectorSize = dataTrack->sectorSize != 0 ? dataTrack->sectorSize : kRawSectorSize;
        m_dataTrackStartLba = dataTrack->startLba;
    }
    else
    {
        m_dataTrackStartLba = 0;
        m_rawSectorSize = kUserDataSize;
        TrackInfo track{};
        track.trackNumber = 1;
        track.type = TrackType::Data;
        track.sectorSize = m_rawSectorSize;
        track.startLba = 0;
        track.file = m_filename;
        m_tracks = {track};
    }

    m_stream.open(m_filename, std::ios::binary);
    if (!m_stream.good())
    {
        addError("Failed to open image file: " + m_filename);
        return false;
    }

    std::error_code error;
    auto fileSize = std::filesystem::file_size(m_filename, error);
    if (error)
    {
        addError("Failed to determine image file size.");
    }
    else if (m_rawSectorSize != 0)
    {
        m_totalSectors = static_cast<u32>(fileSize / m_rawSectorSize);
        if (fileSize % m_rawSectorSize != 0)
        {
            addError("Image file size is not aligned to sector size.");
        }
    }

    if (!isCue && !m_tracks.empty())
    {
        m_tracks.front().sectorSize = m_rawSectorSize;
    }

    return true;
}

void IsoParser::addError(const std::string& message)
{
    if (message.empty())
    {
        return;
    }
    m_errors.push_back(message);
}

bool IsoParser::validateVolumeMetadata(const PrimaryVolumeDescriptor& pvd)
{
    if (pvd.logicalBlockSize == 0)
    {
        addError("Logical block size is zero.");
        return false;
    }
    if ((pvd.logicalBlockSize & (pvd.logicalBlockSize - 1)) != 0)
    {
        addError("Logical block size is not a power of two.");
        return false;
    }
    if (pvd.logicalBlockSize != kUserDataSize)
    {
        addError("Unsupported logical block size.");
        return false;
    }
    if (pvd.volumeSpaceSize == 0)
    {
        addError("Volume space size is zero.");
        return false;
    }
    if (m_totalSectors != 0 && pvd.volumeSpaceSize > m_totalSectors)
    {
        addError("Volume space size exceeds image size.");
        return false;
    }
    return true;
}

bool IsoParser::loadPathTable()
{
    m_pathTable.clear();
    if (m_pvd.pathTableSize == 0 || m_pvd.pathTableLba == 0)
    {
        return true;
    }

    u32 remaining = m_pvd.pathTableSize;
    u32 sector = m_pvd.pathTableLba;
    std::vector<u8> buffer;
    buffer.reserve(m_pvd.pathTableSize);

    while (remaining > 0)
    {
        auto data = readSector(sector);
        if (data.empty())
        {
            addError("Failed to read path table sector.");
            return false;
        }
        u32 toCopy = std::min<u32>(remaining, static_cast<u32>(data.size()));
        buffer.insert(buffer.end(), data.begin(), data.begin() + toCopy);
        remaining -= toCopy;
        ++sector;
    }

    PathTable pathTable{};
    std::string errorMessage;
    if (!pathTable.parse(buffer, errorMessage))
    {
        if (!errorMessage.empty())
        {
            addError(errorMessage);
        }
        return false;
    }

    m_pathTable = pathTable.getPathMap();
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

} // namespace iso
} // namespace psxrecomp
