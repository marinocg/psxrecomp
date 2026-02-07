#include "psxrecomp/iso/iso_parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <sstream>

namespace psxrecomp {
namespace iso {

namespace {

constexpr u32 kUserDataSize = 2048;

u16 readLe16(const u8* data) {
    return static_cast<u16>(data[0]) | (static_cast<u16>(data[1]) << 8);
}

u32 readLe32(const u8* data) {
    return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
           (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
}

std::string trimSpaces(const std::string& value) {
    auto start = value.find_first_not_of(' ');
    if (start == std::string::npos) {
        return "";
    }
    auto end = value.find_last_not_of(' ');
    return value.substr(start, end - start + 1);
}

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string normalizeIsoName(const std::string& name) {
    auto upper = toUpper(name);
    auto semicolon = upper.find(';');
    if (semicolon != std::string::npos) {
        upper.erase(semicolon);
    }
    return upper;
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : path) {
        if (ch == '/' || ch == '\\') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

std::string parseBootPathFromSystemCnf(const std::vector<u8>& systemCnf) {
    if (systemCnf.empty()) {
        return "";
    }

    std::string contents(systemCnf.begin(), systemCnf.end());
    std::istringstream stream(contents);
    std::string line;
    while (std::getline(stream, line)) {
        auto upper = toUpper(line);
        auto pos = upper.find("BOOT");
        if (pos == std::string::npos) {
            continue;
        }
        auto equals = upper.find('=', pos);
        if (equals == std::string::npos) {
            continue;
        }
        std::string value = line.substr(equals + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        auto prefixPos = toUpper(value).find("CDROM:");
        if (prefixPos != std::string::npos) {
            value = value.substr(prefixPos + 6);
        }
        while (!value.empty() && (value[0] == '\\' || value[0] == '/')) {
            value.erase(value.begin());
        }
        std::replace(value.begin(), value.end(), '\\', '/');
        return normalizeIsoName(value);
    }

    return "";
}

} // namespace

IsoParser::IsoParser(const std::string& filename)
    : m_filename(filename),
      m_isOpen(false),
      m_isValid(false),
      m_sectorSize(kUserDataSize),
      m_userDataOffset(0),
      m_stream(),
      m_pvd{},
      m_rootDirectory(),
      m_rootExtent(0),
      m_rootSize(0) {}

IsoParser::~IsoParser() {
    if (m_stream.is_open()) {
        m_stream.close();
    }
}

bool IsoParser::open() {
    if (m_stream.is_open()) {
        m_stream.close();
    }

    m_stream.open(m_filename, std::ios::binary);
    if (!m_stream) {
        return false;
    }

    m_isOpen = readPVD();
    m_isValid = m_isOpen;
    return m_isOpen;
}

std::vector<u8> IsoParser::extractFile(const std::string& path) {
    if (!m_isOpen) {
        return {};
    }

    std::vector<std::string> components = splitPath(path);
    if (components.empty()) {
        return {};
    }

    std::vector<DirectoryRecord> records;
    if (!readDirectory(m_rootExtent, m_rootSize, records)) {
        return {};
    }

    DirectoryRecord target{};
    for (size_t i = 0; i < components.size(); ++i) {
        std::string desired = normalizeIsoName(components[i]);
        bool found = false;
        for (const auto& record : records) {
            if (record.name.empty()) {
                continue;
            }
            if (record.name == "." || record.name == "..") {
                continue;
            }
            if (normalizeIsoName(record.name) == desired) {
                target = record;
                found = true;
                break;
            }
        }

        if (!found) {
            return {};
        }

        if (i < components.size() - 1) {
            if ((target.flags & 0x02) == 0) {
                return {};
            }
            records.clear();
            if (!readDirectory(target.extentLocation, target.dataLength, records)) {
                return {};
            }
        }
    }

    if ((target.flags & 0x02) != 0) {
        return {};
    }

    std::vector<u8> data;
    data.reserve(target.dataLength);
    u32 remaining = target.dataLength;
    u32 sector = target.extentLocation;
    while (remaining > 0) {
        auto sectorData = readSector(sector);
        if (sectorData.empty()) {
            return {};
        }
        u32 toCopy = std::min<u32>(remaining, static_cast<u32>(sectorData.size()));
        data.insert(data.end(), sectorData.begin(), sectorData.begin() + toCopy);
        remaining -= toCopy;
        ++sector;
    }

    return data;
}

std::string IsoParser::findExecutable() {
    if (!m_isOpen) {
        return "";
    }

    auto systemCnf = extractFile("SYSTEM.CNF");
    auto bootPath = parseBootPathFromSystemCnf(systemCnf);
    if (!bootPath.empty()) {
        return bootPath;
    }

    std::vector<DirectoryRecord> records;
    if (!readDirectory(m_rootExtent, m_rootSize, records)) {
        return "";
    }

    for (const auto& record : records) {
        if ((record.flags & 0x02) != 0) {
            continue;
        }
        auto name = normalizeIsoName(record.name);
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".EXE") {
            return name;
        }
    }

    return "";
}

std::string IsoParser::getVolumeLabel() const {
    std::string label(m_pvd.volumeId, m_pvd.volumeId + sizeof(m_pvd.volumeId));
    auto nullPos = label.find('\0');
    if (nullPos != std::string::npos) {
        label.erase(nullPos);
    }
    return trimSpaces(label);
}

bool IsoParser::isValid() const {
    return m_isValid;
}

bool IsoParser::readPVD() {
    struct Layout {
        u32 sectorSize;
        u32 userDataOffset;
    };

    const Layout layouts[] = {
        {2048, 0},
        {2352, 16},
        {2352, 24},
    };

    std::array<u8, kUserDataSize> sector{};

    for (const auto& layout : layouts) {
        m_sectorSize = layout.sectorSize;
        m_userDataOffset = layout.userDataOffset;
        if (!readSectorInto(16, sector.data(), sector.size())) {
            continue;
        }
        if (sector[0] != 1) {
            continue;
        }
        if (std::memcmp(sector.data() + 1, "CD001", 5) != 0) {
            continue;
        }

        std::memcpy(m_pvd.identifier, sector.data() + 1, 5);
        m_pvd.type = sector[0];
        m_pvd.version = sector[6];
        std::memcpy(m_pvd.systemId, sector.data() + 8, 32);
        std::memcpy(m_pvd.volumeId, sector.data() + 40, 32);
        m_pvd.volumeSpaceSize = readLe32(sector.data() + 80);
        m_pvd.volumeSetSize = readLe16(sector.data() + 120);
        m_pvd.volumeSequenceNumber = readLe16(sector.data() + 124);
        m_pvd.logicalBlockSize = readLe16(sector.data() + 128);
        m_pvd.pathTableSize = readLe32(sector.data() + 132);

        const u8* rootRecord = sector.data() + 156;
        m_rootExtent = readLe32(rootRecord + 2);
        m_rootSize = readLe32(rootRecord + 10);
        m_rootDirectory.clear();
        if (!readDirectory(m_rootExtent, m_rootSize, m_rootDirectory)) {
            return false;
        }

        return true;
    }

    return false;
}

bool IsoParser::readDirectory(u32 extent, u32 size, std::vector<DirectoryRecord>& records) {
    if (size == 0) {
        return false;
    }

    records.clear();
    u32 remaining = size;
    u32 sector = extent;
    std::vector<u8> buffer;
    buffer.reserve(size);

    while (remaining > 0) {
        auto data = readSector(sector);
        if (data.empty()) {
            return false;
        }
        u32 toCopy = std::min<u32>(remaining, static_cast<u32>(data.size()));
        buffer.insert(buffer.end(), data.begin(), data.begin() + toCopy);
        remaining -= toCopy;
        ++sector;
    }

    size_t offset = 0;
    while (offset < buffer.size()) {
        u8 length = buffer[offset];
        if (length == 0) {
            size_t nextBoundary = ((offset / kUserDataSize) + 1) * kUserDataSize;
            if (nextBoundary <= offset) {
                break;
            }
            offset = std::min(nextBoundary, buffer.size());
            continue;
        }
        if (offset + length > buffer.size()) {
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
        if (record.nameLength > 0 && 33 + record.nameLength <= length) {
            record.name.assign(reinterpret_cast<const char*>(recordData + 33), record.nameLength);
            if (record.nameLength == 1) {
                if (record.name[0] == '\0') {
                    record.name = ".";
                } else if (static_cast<unsigned char>(record.name[0]) == 1) {
                    record.name = "..";
                }
            }
        }
        records.push_back(record);
        offset += length;
    }

    return true;
}

std::vector<u8> IsoParser::readSector(u32 sector) {
    std::vector<u8> data(kUserDataSize);
    if (!readSectorInto(sector, data.data(), data.size())) {
        return {};
    }
    return data;
}

bool IsoParser::readSectorInto(u32 sector, u8* buffer, size_t size) {
    if (!m_stream) {
        return false;
    }
    std::streamoff offset = static_cast<std::streamoff>(sector) * static_cast<std::streamoff>(m_sectorSize) +
                            static_cast<std::streamoff>(m_userDataOffset);
    m_stream.seekg(offset, std::ios::beg);
    if (!m_stream.good()) {
        return false;
    }
    m_stream.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return m_stream.gcount() == static_cast<std::streamsize>(size);
}

} // namespace iso
} // namespace psxrecomp
