#include "psxrecomp/iso/iso_parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <optional>
#include <sstream>

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

u16 readLe16(const u8* data)
{
    return static_cast<u16>(data[0]) | (static_cast<u16>(data[1]) << 8);
}

u32 readLe32(const u8* data)
{
    return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
           (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
}

std::string trimSpaces(const std::string& value)
{
    auto start = value.find_first_not_of(' ');
    if (start == std::string::npos)
    {
        return "";
    }
    auto end = value.find_last_not_of(' ');
    return value.substr(start, end - start + 1);
}

std::string toUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string normalizeIsoName(const std::string& name)
{
    auto upper = toUpper(name);
    auto semicolon = upper.find(';');
    if (semicolon != std::string::npos)
    {
        upper.erase(semicolon);
    }
    return upper;
}

std::vector<std::string> splitPath(const std::string& path)
{
    std::vector<std::string> parts;
    std::string current;
    for (char ch : path)
    {
        if (ch == '/' || ch == '\\')
        {
            if (!current.empty())
            {
                parts.push_back(current);
                current.clear();
            }
        }
        else
        {
            current.push_back(ch);
        }
    }
    if (!current.empty())
    {
        parts.push_back(current);
    }
    return parts;
}

std::string trim(const std::string& value)
{
    auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
        return "";
    }
    auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string decodeJolietName(const u8* data, size_t length)
{
    std::string result;
    result.reserve(length / 2);
    for (size_t i = 0; i + 1 < length; i += 2)
    {
        u16 code = static_cast<u16>(data[i] << 8) | static_cast<u16>(data[i + 1]);
        if (code == 0)
        {
            continue;
        }
        if (code <= 0x7F)
        {
            result.push_back(static_cast<char>(code));
        }
        else
        {
            result.push_back('?');
        }
    }
    return result;
}

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
        auto upper = toUpper(line);
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
        auto prefixPos = toUpper(value).find("CDROM:");
        if (prefixPos != std::string::npos)
        {
            value = value.substr(prefixPos + 6);
        }
        while (!value.empty() && (value[0] == '\\' || value[0] == '/'))
        {
            value.erase(value.begin());
        }
        std::replace(value.begin(), value.end(), '\\', '/');
        return normalizeIsoName(value);
    }

    return "";
}

} // namespace

IsoParser::IsoParser(const std::string& filename)
    : m_filename(filename), m_isOpen(false), m_isValid(false), m_rawSectorSize(kUserDataSize),
      m_dataTrackStartLba(0), m_logicalBlockSize(kUserDataSize), m_useJoliet(false), m_stream(),
      m_pvd{}, m_rootDirectory(), m_rootExtent(0), m_rootSize(0)
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

    std::vector<std::string> components = splitPath(path);
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
    for (size_t i = 0; i < components.size(); ++i)
    {
        std::string desired = normalizeIsoName(components[i]);
        bool found = false;
        targetExtents.clear();
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
            if (normalizeIsoName(record.name) == desired)
            {
                target = record;
                targetExtents.push_back(record);
                found = true;
            }
        }

        if (!found)
        {
            return {};
        }

        if (i < components.size() - 1)
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
        }
    }

    if ((target.flags & 0x02) != 0)
    {
        return {};
    }

    std::vector<u8> data;
    if (targetExtents.empty())
    {
        targetExtents.push_back(target);
    }

    std::sort(targetExtents.begin(), targetExtents.end(),
              [](const DirectoryRecord& lhs, const DirectoryRecord& rhs)
              { return lhs.extentLocation < rhs.extentLocation; });

    u32 totalLength = 0;
    for (const auto& extent : targetExtents)
    {
        totalLength += extent.dataLength;
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

    std::vector<DirectoryRecord> records;
    if (!readDirectory(m_rootExtent, m_rootSize, records))
    {
        return "";
    }

    for (const auto& record : records)
    {
        if ((record.flags & 0x02) != 0)
        {
            continue;
        }
        auto name = normalizeIsoName(record.name);
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".EXE")
        {
            return name;
        }
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
    return trimSpaces(label);
}

bool IsoParser::isValid() const
{
    return m_isValid;
}

bool IsoParser::readPVD()
{
    std::array<u8, kUserDataSize> sector{};
    std::optional<PrimaryVolumeDescriptor> pvd;
    std::optional<std::pair<u32, u32>> pvdRoot;
    std::optional<std::pair<u32, u32>> jolietRoot;

    u32 trackStart = m_dataTrackStartLba;
    m_useJoliet = false;

    for (u32 layoutSize : {kUserDataSize, kRawSectorSize})
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
                candidate.volumeSpaceSize = readLe32(sector.data() + 80);
                candidate.volumeSetSize = readLe16(sector.data() + 120);
                candidate.volumeSequenceNumber = readLe16(sector.data() + 124);
                candidate.logicalBlockSize = readLe16(sector.data() + 128);
                candidate.pathTableSize = readLe32(sector.data() + 132);
                pvd = candidate;
                const u8* rootRecord = sector.data() + 156;
                pvdRoot = std::make_pair(readLe32(rootRecord + 2), readLe32(rootRecord + 10));
            }
            if (type == 2)
            {
                if (sector[88] == 0x25 && sector[89] == 0x2F &&
                    (sector[90] == 0x40 || sector[90] == 0x43 || sector[90] == 0x45))
                {
                    const u8* rootRecord = sector.data() + 156;
                    u32 rootExtent = readLe32(rootRecord + 2);
                    u32 rootSize = readLe32(rootRecord + 10);
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
            break;
        }

        const u8* recordData = buffer.data() + offset;
        DirectoryRecord record{};
        record.length = recordData[0];
        record.extendedLength = recordData[1];
        record.extentLocation = readLe32(recordData + 2);
        record.dataLength = readLe32(recordData + 10);
        std::memcpy(record.recordingDateTime, recordData + 18, 7);
        record.flags = recordData[25];
        record.fileUnitSize = recordData[26];
        record.interleaveGapSize = recordData[27];
        record.volumeSequenceNumber = readLe16(recordData + 28);
        record.nameLength = recordData[32];
        if (record.nameLength > 0 && 33 + record.nameLength <= length)
        {
            if (m_useJoliet)
            {
                record.name = decodeJolietName(recordData + 33, record.nameLength);
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
        return {};
    }
    if (m_rawSectorSize == kUserDataSize)
    {
        return raw;
    }
    auto view = decodeSectorLayout(raw);
    if (view.size == 0 || view.offset + view.size > raw.size())
    {
        return {};
    }
    return std::vector<u8>(raw.begin() + static_cast<std::ptrdiff_t>(view.offset),
                           raw.begin() + static_cast<std::ptrdiff_t>(view.offset + view.size));
}

bool IsoParser::readRawSector(u32 sector, std::vector<u8>& buffer)
{
    if (!m_stream)
    {
        return false;
    }
    if (buffer.size() != m_rawSectorSize)
    {
        buffer.resize(m_rawSectorSize);
    }
    std::streamoff offset = static_cast<std::streamoff>(sector + m_dataTrackStartLba) *
                            static_cast<std::streamoff>(m_rawSectorSize);
    m_stream.seekg(offset, std::ios::beg);
    if (!m_stream.good())
    {
        return false;
    }
    m_stream.read(reinterpret_cast<char*>(buffer.data()),
                  static_cast<std::streamsize>(buffer.size()));
    return m_stream.gcount() == static_cast<std::streamsize>(buffer.size());
}

bool IsoParser::readSectorInto(u32 sector, u8* buffer, size_t size)
{
    if (!m_stream)
    {
        return false;
    }
    if (m_rawSectorSize == kUserDataSize)
    {
        std::vector<u8> raw(m_rawSectorSize);
        if (!readRawSector(sector, raw))
        {
            return false;
        }
        if (size > raw.size())
        {
            return false;
        }
        std::memcpy(buffer, raw.data(), size);
        return true;
    }

    std::vector<u8> raw(m_rawSectorSize);
    if (!readRawSector(sector, raw))
    {
        return false;
    }
    auto view = decodeSectorLayout(raw);
    if (view.size < size || view.offset + size > raw.size())
    {
        return false;
    }
    std::memcpy(buffer, raw.data() + view.offset, size);
    return true;
}

bool IsoParser::openStream()
{
    if (m_stream.is_open())
    {
        m_stream.close();
    }

    if (loadCueSheet())
    {
        m_stream.open(m_filename, std::ios::binary);
        return m_stream.good();
    }

    m_dataTrackStartLba = 0;
    m_stream.open(m_filename, std::ios::binary);
    return m_stream.good();
}

bool IsoParser::loadCueSheet()
{
    auto extensionPos = m_filename.find_last_of('.');
    if (extensionPos == std::string::npos)
    {
        return false;
    }
    std::string extension = toUpper(m_filename.substr(extensionPos + 1));
    if (extension != "CUE")
    {
        return false;
    }

    std::ifstream cueStream(m_filename);
    if (!cueStream)
    {
        return false;
    }

    std::filesystem::path cuePath(m_filename);
    std::string line;
    std::string currentFile;
    struct TrackInfo
    {
        u32 sectorSize = 0;
        u32 index01 = 0;
        std::string file;
    };
    std::optional<TrackInfo> dataTrack;

    while (std::getline(cueStream, line))
    {
        auto trimmed = trim(line);
        if (trimmed.empty())
        {
            continue;
        }
        auto upper = toUpper(trimmed);
        if (upper.rfind("FILE", 0) == 0)
        {
            auto firstQuote = trimmed.find('"');
            auto lastQuote = trimmed.find_last_of('"');
            if (firstQuote != std::string::npos && lastQuote != std::string::npos &&
                lastQuote > firstQuote)
            {
                currentFile = trimmed.substr(firstQuote + 1, lastQuote - firstQuote - 1);
            }
            continue;
        }
        if (upper.rfind("TRACK", 0) == 0)
        {
            std::istringstream stream(trimmed);
            std::string token;
            std::string type;
            stream >> token;
            stream >> token;
            stream >> type;
            type = toUpper(type);
            if (type == "MODE1/2048")
            {
                dataTrack = TrackInfo{2048, 0, currentFile};
            }
            else if (type == "MODE2/2352")
            {
                dataTrack = TrackInfo{2352, 0, currentFile};
            }
            continue;
        }
        if (upper.rfind("INDEX 01", 0) == 0 && dataTrack)
        {
            std::istringstream stream(trimmed);
            std::string token;
            std::string timecode;
            stream >> token;
            stream >> token;
            stream >> timecode;
            int minutes = 0;
            int seconds = 0;
            int frames = 0;
            char separator = '\0';
            std::istringstream timeStream(timecode);
            timeStream >> minutes >> separator >> seconds >> separator >> frames;
            u32 lba = static_cast<u32>(minutes * 60 * 75 + seconds * 75 + frames);
            dataTrack->index01 = lba;
        }
    }

    if (!dataTrack || dataTrack->file.empty())
    {
        return false;
    }

    std::filesystem::path binPath = cuePath.parent_path() / dataTrack->file;
    m_filename = binPath.string();
    m_rawSectorSize = dataTrack->sectorSize != 0 ? dataTrack->sectorSize : kRawSectorSize;
    m_dataTrackStartLba = dataTrack->index01;
    return true;
}

} // namespace iso
} // namespace psxrecomp
