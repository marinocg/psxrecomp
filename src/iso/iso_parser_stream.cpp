#include "psxrecomp/iso/iso_parser.h"

#include "cue_sheet.h"
#include "iso_sector.h"
#include "iso_utils.h"
#include "path_table.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <unordered_set>

namespace psxrecomp
{
namespace iso
{

namespace
{

constexpr u32 kUserDataSize = detail::kUserDataSize;
constexpr u32 kRawSectorSize = detail::kRawSectorSize;

} // namespace

std::vector<u8> IsoParser::readSector(u32 sector)
{
    std::vector<u8> cached;
    if (fetchSectorCache(sector, cached, m_userSectorCacheIndex, m_userSectorCache))
    {
        return cached;
    }
    std::vector<u8> raw(m_rawSectorSize);
    if (!readRawSector(sector, raw))
    {
        addError("Failed to read raw sector.");
        return {};
    }
    if (m_rawSectorSize == kUserDataSize)
    {
        storeSectorCache(sector, raw, m_userSectorCacheIndex, m_userSectorCache);
        return raw;
    }
    auto view = detail::decodeSectorLayout(raw);
    if (view.size == 0)
    {
        if (m_userDataOffset + kUserDataSize > raw.size())
        {
            addError("Unsupported sector layout.");
            return {};
        }
        view.offset = m_userDataOffset;
        view.size = kUserDataSize;
    }
    if (view.offset + view.size > raw.size())
    {
        addError("Unsupported sector layout.");
        return {};
    }
    std::vector<u8> data(raw.begin() + static_cast<std::ptrdiff_t>(view.offset),
                         raw.begin() + static_cast<std::ptrdiff_t>(view.offset + view.size));
    storeSectorCache(sector, data, m_userSectorCacheIndex, m_userSectorCache);
    return data;
}

bool IsoParser::readRawSector(u32 sector, std::vector<u8>& buffer)
{
    if (!m_stream)
    {
        addError("Stream not open for sector read.");
        return false;
    }
    if (fetchSectorCache(sector, buffer, m_rawSectorCacheIndex, m_rawSectorCache))
    {
        return true;
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
    storeSectorCache(sector, buffer, m_rawSectorCacheIndex, m_rawSectorCache);
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
    auto view = detail::decodeSectorLayout(m_rawSectorScratch);
    if (view.size == 0)
    {
        if (m_userDataOffset + size > m_rawSectorScratch.size())
        {
            addError("Invalid sector view.");
            return false;
        }
        view.offset = m_userDataOffset;
        view.size = kUserDataSize;
    }
    if (view.size < size || view.offset + size > m_rawSectorScratch.size())
    {
        addError("Invalid sector view.");
        return false;
    }
    std::memcpy(buffer, m_rawSectorScratch.data() + view.offset, size);
    return true;
}

bool IsoParser::readUserDataSector(u32 lba, std::vector<u8>& outData)
{
    return readSectorUser2048(lba, outData);
}

bool IsoParser::canReadUser2048() const
{
    return m_isOpen;
}

bool IsoParser::canReadRaw2352() const
{
    return m_isOpen && m_rawSectorSize == kRawSectorSize;
}

bool IsoParser::readSectorUser2048(u32 lba, std::vector<u8>& out2048)
{
    out2048.resize(kUserDataSize);
    if (!readSectorInto(lba, out2048.data(), out2048.size()))
    {
        out2048.clear();
        return false;
    }
    return true;
}

bool IsoParser::readSectorRaw2352(u32 lba, std::vector<u8>& out2352)
{
    out2352.clear();
    if (!canReadRaw2352())
    {
        addError("Raw 2352-byte sector reads are unavailable for this image.");
        return false;
    }
    if (!readRawSector(lba, out2352))
    {
        return false;
    }
    if (out2352.size() != kRawSectorSize)
    {
        addError("Unexpected raw sector size while reading 2352-byte sector.");
        out2352.clear();
        return false;
    }
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
    m_filename = m_inputFilename;
    std::filesystem::path inputPath(m_inputFilename);
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
        const auto* dataTrack = cueSheet.findPrimaryDataTrack();
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
            track.discIndex = 0;
        }

        std::filesystem::path binPath = inputPath.parent_path() / dataTrack->file;
        m_filename = binPath.string();
        m_rawSectorSize = dataTrack->sectorSize != 0 ? dataTrack->sectorSize : kRawSectorSize;
        m_userDataOffset = 0;
        m_dataTrackStartLba = dataTrack->startLba;
    }
    else
    {
        m_dataTrackStartLba = 0;
        m_rawSectorSize = kUserDataSize;
        m_userDataOffset = 0;
        TrackInfo track{};
        track.trackNumber = 1;
        track.type = TrackType::Data;
        track.sectorSize = m_rawSectorSize;
        track.startLba = 0;
        track.sessionNumber = 1;
        track.discIndex = 0;
        track.file = m_filename;
        m_tracks = {track};
    }

    m_stream.open(m_filename, std::ios::binary);
    if (!m_stream.good())
    {
        addError("Failed to open image file: " + m_filename);
        return false;
    }

    if (isCue)
    {
        m_totalSectors = 0;
        std::unordered_set<std::string> uniqueTrackFiles;
        for (const auto& track : m_tracks)
        {
            if (track.file.empty() || track.sectorSize == 0)
            {
                continue;
            }
            if (!uniqueTrackFiles.insert(track.file).second)
            {
                continue;
            }

            std::error_code trackError;
            const auto trackFileSize = std::filesystem::file_size(track.file, trackError);
            if (trackError)
            {
                addError("Failed to determine track file size: " + track.file);
                continue;
            }

            if (trackFileSize % static_cast<u64>(track.sectorSize) != 0)
            {
                addError("Track file size is not aligned to sector size: " + track.file);
            }

            const u64 trackSectors = trackFileSize / static_cast<u64>(track.sectorSize);
            if (trackSectors > static_cast<u64>(std::numeric_limits<u32>::max()) -
                                   static_cast<u64>(m_totalSectors))
            {
                addError("Total cue sector count exceeds supported range.");
                m_totalSectors = std::numeric_limits<u32>::max();
                break;
            }
            m_totalSectors += static_cast<u32>(trackSectors);
        }
    }
    else
    {
        m_totalSectors = 0;
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

void IsoParser::setSectorCacheCapacity(size_t capacity)
{
    m_sectorCacheCapacity = capacity;
    clearSectorCache();
}

void IsoParser::clearSectorCache()
{
    m_rawSectorCache.clear();
    m_rawSectorCacheIndex.clear();
    m_userSectorCache.clear();
    m_userSectorCacheIndex.clear();
}

bool IsoParser::fetchSectorCache(u32 sector, std::vector<u8>& buffer,
                                 std::unordered_map<u32, std::list<CachedSector>::iterator>& index,
                                 std::list<CachedSector>& entries)
{
    if (m_sectorCacheCapacity == 0)
    {
        return false;
    }
    auto it = index.find(sector);
    if (it == index.end())
    {
        return false;
    }
    auto listIt = it->second;
    buffer = listIt->data;
    entries.splice(entries.begin(), entries, listIt);
    return true;
}

void IsoParser::storeSectorCache(u32 sector, const std::vector<u8>& buffer,
                                 std::unordered_map<u32, std::list<CachedSector>::iterator>& index,
                                 std::list<CachedSector>& entries)
{
    if (m_sectorCacheCapacity == 0)
    {
        return;
    }
    auto it = index.find(sector);
    if (it != index.end())
    {
        auto listIt = it->second;
        listIt->data = buffer;
        entries.splice(entries.begin(), entries, listIt);
        return;
    }
    if (entries.size() >= m_sectorCacheCapacity)
    {
        auto& last = entries.back();
        index.erase(last.sector);
        entries.pop_back();
    }
    entries.push_front(CachedSector{sector, buffer});
    index[sector] = entries.begin();
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
    return true;
}

bool IsoParser::loadPathTable()
{
    m_pathTable.clear();
    if (m_pvd.pathTableSize == 0)
    {
        return true;
    }

    if (m_totalSectors != 0 && m_logicalBlockSize != 0)
    {
        u64 maxBytes = static_cast<u64>(m_totalSectors) * static_cast<u64>(m_logicalBlockSize);
        if (static_cast<u64>(m_pvd.pathTableSize) > maxBytes)
        {
            addError("Path table size exceeds image size.");
            return false;
        }
    }

    u32 pathTableLba = m_pvd.pathTableLba != 0 ? m_pvd.pathTableLba : m_pvd.optionalPathTableLba;
    if (pathTableLba == 0)
    {
        return true;
    }

    u32 remaining = m_pvd.pathTableSize;
    u32 sector = pathTableLba;
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

} // namespace iso
} // namespace psxrecomp
