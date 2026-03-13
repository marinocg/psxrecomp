/**
 * @file bios_file_table.cpp
 * @brief Implementation of the read-only BIOS file table.
 *
 * Parses ISO 9660 directory structures directly from Disc* sector
 * reads to resolve paths and serve data for B0:32–36 and B0:42–43.
 */
#include "psxrecomp/runtime/bios_file_table.h"

#include "psxrecomp/runtime/disc.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u32 SECTOR_SIZE = 2048;
constexpr u32 PVD_SECTOR = 16; // ISO 9660 Primary Volume Descriptor.

/// Read a little-endian u32 from a byte pointer.
u32 readLE32(const u8* p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

} // namespace

// ---------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------

void BiosFileTable::reset()
{
    m_fds = {};
    m_pvdLoaded = false;
    m_rootLba = 0;
    m_rootSize = 0;
    m_dirCache.clear();
    m_dirIndex = 0;
}

void BiosFileTable::setDisc(Disc* disc)
{
    m_disc = disc;
    m_pvdLoaded = false;
}

// ---------------------------------------------------------------
// Path normalisation
// ---------------------------------------------------------------

std::string BiosFileTable::normalisePath(const std::string& rawPath)
{
    std::string path = rawPath;

    // Strip device prefix: "cdrom:", "cdrom0:", "bu:", etc.
    auto colonPos = path.find(':');
    if (colonPos != std::string::npos)
    {
        path = path.substr(colonPos + 1);
    }

    // Replace backslashes with forward slashes.
    for (char& ch : path)
    {
        if (ch == '\\')
        {
            ch = '/';
        }
    }

    // Strip leading slashes.
    while (!path.empty() && path[0] == '/')
    {
        path.erase(path.begin());
    }

    // Upper-case for ISO 9660 matching.
    for (char& ch : path)
    {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    return path;
}

std::string BiosFileTable::stripVersionSuffix(const std::string& name)
{
    auto semi = name.find(';');
    if (semi != std::string::npos)
    {
        return name.substr(0, semi);
    }
    return name;
}

// ---------------------------------------------------------------
// ISO 9660 helpers
// ---------------------------------------------------------------

bool BiosFileTable::ensurePvd()
{
    if (m_pvdLoaded)
    {
        return true;
    }
    if (!m_disc)
    {
        return false;
    }

    std::array<u8, SECTOR_SIZE> sector{};
    if (!m_disc->readUserSector(PVD_SECTOR, std::span<u8, SECTOR_SIZE>(sector)))
    {
        return false;
    }

    // Verify PVD signature: type 1, "CD001", version 1.
    if (sector[0] != 1 || std::memcmp(sector.data() + 1, "CD001", 5) != 0 || sector[6] != 1)
    {
        return false;
    }

    // Root directory record starts at offset 156 in the PVD.
    const u8* rootRecord = sector.data() + 156;
    m_rootLba = readLE32(rootRecord + 2);
    m_rootSize = readLE32(rootRecord + 10);

    m_pvdLoaded = true;
    return true;
}

bool BiosFileTable::findFileInDirectory(u32 dirLba, u32 dirSize, const std::string& name,
                                        u32& outLba, u32& outSize)
{
    if (!m_disc)
    {
        return false;
    }

    const u32 sectorCount = (dirSize + SECTOR_SIZE - 1) / SECTOR_SIZE;
    std::array<u8, SECTOR_SIZE> sector{};

    for (u32 s = 0; s < sectorCount; ++s)
    {
        if (!m_disc->readUserSector(dirLba + s, std::span<u8, SECTOR_SIZE>(sector)))
        {
            return false;
        }

        u32 offset = 0;
        while (offset < SECTOR_SIZE)
        {
            const u8 recordLen = sector[offset];
            if (recordLen == 0)
            {
                break; // no more records in this sector
            }
            if (offset + recordLen > SECTOR_SIZE)
            {
                break;
            }

            const u8 nameLen = sector[offset + 32];
            if (nameLen > 0 && offset + 33 + nameLen <= SECTOR_SIZE)
            {
                std::string entryName(reinterpret_cast<const char*>(sector.data() + offset + 33),
                                      nameLen);

                // Upper-case for comparison.
                for (char& ch : entryName)
                {
                    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                }

                // Match with and without version suffix.
                if (stripVersionSuffix(entryName) == name || entryName == name)
                {
                    outLba = readLE32(sector.data() + offset + 2);
                    outSize = readLE32(sector.data() + offset + 10);
                    return true;
                }
            }
            offset += recordLen;
        }
    }
    return false;
}

bool BiosFileTable::resolveIsoPath(const std::string& normalised, u32& outLba, u32& outSize)
{
    if (!ensurePvd())
    {
        return false;
    }

    // Split path into components.
    std::vector<std::string> components;
    {
        std::string token;
        for (char ch : normalised)
        {
            if (ch == '/')
            {
                if (!token.empty())
                {
                    components.push_back(token);
                    token.clear();
                }
            }
            else
            {
                token.push_back(ch);
            }
        }
        if (!token.empty())
        {
            components.push_back(token);
        }
    }

    if (components.empty())
    {
        return false;
    }

    u32 currentLba = m_rootLba;
    u32 currentSize = m_rootSize;

    for (size_t i = 0; i < components.size(); ++i)
    {
        u32 foundLba = 0;
        u32 foundSize = 0;
        if (!findFileInDirectory(currentLba, currentSize, components[i], foundLba, foundSize))
        {
            return false;
        }
        currentLba = foundLba;
        currentSize = foundSize;
    }

    outLba = currentLba;
    outSize = currentSize;
    return true;
}

bool BiosFileTable::listDirectory(u32 dirLba, u32 dirSize, std::vector<BiosDirEntry>& entries)
{
    if (!m_disc)
    {
        return false;
    }

    entries.clear();
    const u32 sectorCount = (dirSize + SECTOR_SIZE - 1) / SECTOR_SIZE;
    std::array<u8, SECTOR_SIZE> sector{};

    for (u32 s = 0; s < sectorCount; ++s)
    {
        if (!m_disc->readUserSector(dirLba + s, std::span<u8, SECTOR_SIZE>(sector)))
        {
            return false;
        }

        u32 offset = 0;
        while (offset < SECTOR_SIZE)
        {
            const u8 recordLen = sector[offset];
            if (recordLen == 0)
            {
                break;
            }
            if (offset + recordLen > SECTOR_SIZE)
            {
                break;
            }

            const u8 nameLen = sector[offset + 32];
            if (nameLen > 0 && offset + 33 + nameLen <= SECTOR_SIZE)
            {
                // Skip self/parent entries (single byte 0x00 or 0x01).
                if (nameLen == 1 && (sector[offset + 33] == 0x00 || sector[offset + 33] == 0x01))
                {
                    offset += recordLen;
                    continue;
                }

                BiosDirEntry entry;
                entry.name.assign(reinterpret_cast<const char*>(sector.data() + offset + 33),
                                  nameLen);
                entry.lba = readLE32(sector.data() + offset + 2);
                entry.size = readLE32(sector.data() + offset + 10);
                entry.flags = sector[offset + 25];
                entries.push_back(std::move(entry));
            }
            offset += recordLen;
        }
    }
    return true;
}

// ---------------------------------------------------------------
// BIOS file operations
// ---------------------------------------------------------------

int BiosFileTable::fileOpen(const std::string& rawPath)
{
    const std::string normalised = normalisePath(rawPath);
    if (normalised.empty())
    {
        return -1;
    }

    u32 lba = 0;
    u32 size = 0;
    if (!resolveIsoPath(normalised, lba, size))
    {
        return -1;
    }

    // Allocate the first free FD (starting from 2).
    for (int fd = FIRST_USER_FD; fd < static_cast<int>(MAX_FDS); ++fd)
    {
        if (!m_fds[static_cast<size_t>(fd)].open)
        {
            auto& f = m_fds[static_cast<size_t>(fd)];
            f.open = true;
            f.lba = lba;
            f.size = size;
            f.position = 0;
            f.path = normalised;
            return fd;
        }
    }
    return -1; // no free descriptors
}

int BiosFileTable::fileSeek(int fd, int offset, int whence)
{
    if (fd < 0 || fd >= static_cast<int>(MAX_FDS))
    {
        return -1;
    }
    auto& f = m_fds[static_cast<size_t>(fd)];
    if (!f.open)
    {
        return -1;
    }

    int newPos = 0;
    switch (whence)
    {
    case 0: // SEEK_SET
        newPos = offset;
        break;
    case 1: // SEEK_CUR
        newPos = static_cast<int>(f.position) + offset;
        break;
    case 2: // SEEK_END
        newPos = static_cast<int>(f.size) + offset;
        break;
    default:
        return -1;
    }

    if (newPos < 0)
    {
        newPos = 0;
    }
    if (static_cast<u32>(newPos) > f.size)
    {
        newPos = static_cast<int>(f.size);
    }

    f.position = static_cast<u32>(newPos);
    return newPos;
}

int BiosFileTable::fileRead(int fd, u8* dst, u32 count)
{
    std::vector<u8> buffer;
    const int result = fileRead(fd, buffer, count);
    if (result <= 0)
    {
        return result;
    }

    std::memcpy(dst, buffer.data(), static_cast<size_t>(result));
    return result;
}

int BiosFileTable::fileRead(int fd, std::vector<u8>& out, u32 count)
{
    out.clear();
    if (fd < 0 || fd >= static_cast<int>(MAX_FDS))
    {
        return -1;
    }
    auto& f = m_fds[static_cast<size_t>(fd)];
    if (!f.open)
    {
        return -1;
    }
    if (!m_disc)
    {
        return -1;
    }

    // Clamp to remaining bytes.
    if (f.position >= f.size)
    {
        return 0;
    }
    const u32 remaining = f.size - f.position;
    const u32 toRead = std::min(count, remaining);
    if (toRead == 0)
    {
        return 0;
    }

    out.resize(toRead);

    u32 bytesRead = 0;
    std::array<u8, SECTOR_SIZE> sectorBuf{};

    while (bytesRead < toRead)
    {
        const u32 fileOffset = f.position + bytesRead;
        const u32 sectorIndex = fileOffset / SECTOR_SIZE;
        const u32 offsetInSector = fileOffset % SECTOR_SIZE;
        const u32 sectorLba = f.lba + sectorIndex;

        if (!m_disc->readUserSector(sectorLba, std::span<u8, SECTOR_SIZE>(sectorBuf)))
        {
            break;
        }

        const u32 bytesAvail = SECTOR_SIZE - offsetInSector;
        const u32 chunk = std::min(toRead - bytesRead, bytesAvail);
        std::memcpy(out.data() + bytesRead, sectorBuf.data() + offsetInSector, chunk);
        bytesRead += chunk;
    }

    f.position += bytesRead;
    out.resize(bytesRead);
    return static_cast<int>(bytesRead);
}

bool BiosFileTable::fileClose(int fd)
{
    if (fd < 0 || fd >= static_cast<int>(MAX_FDS))
    {
        return false;
    }
    auto& f = m_fds[static_cast<size_t>(fd)];
    if (!f.open)
    {
        return false;
    }
    f = {};
    return true;
}

const BiosDirEntry* BiosFileTable::firstFile(const std::string& pattern)
{
    m_dirCache.clear();
    m_dirIndex = 0;

    if (!ensurePvd())
    {
        return nullptr;
    }

    // The pattern can be "cdrom:\\*" or a directory path.
    // Normalise to extract just the directory part.
    std::string normalised = normalisePath(pattern);

    // Strip trailing wildcard ("*" or "\\*").
    if (!normalised.empty() && normalised.back() == '*')
    {
        normalised.pop_back();
        if (!normalised.empty() && normalised.back() == '/')
        {
            normalised.pop_back();
        }
    }

    u32 dirLba = m_rootLba;
    u32 dirSize = m_rootSize;

    // If a subdirectory was specified, resolve it.
    if (!normalised.empty())
    {
        if (!resolveIsoPath(normalised, dirLba, dirSize))
        {
            return nullptr;
        }
    }

    if (!listDirectory(dirLba, dirSize, m_dirCache))
    {
        return nullptr;
    }

    if (m_dirCache.empty())
    {
        return nullptr;
    }

    m_dirIndex = 1;
    return &m_dirCache[0];
}

const BiosDirEntry* BiosFileTable::nextFile()
{
    if (m_dirIndex >= m_dirCache.size())
    {
        return nullptr;
    }
    return &m_dirCache[m_dirIndex++];
}

const BiosFileDescriptor* BiosFileTable::getFd(int fd) const
{
    if (fd < 0 || fd >= static_cast<int>(MAX_FDS))
    {
        return nullptr;
    }
    const auto& f = m_fds[static_cast<size_t>(fd)];
    return f.open ? &f : nullptr;
}

} // namespace runtime
} // namespace psxrecomp
