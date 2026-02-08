#include "psxrecomp/iso/iso_parser.h"

#include "cue_sheet.h"
#include "iso_sector.h"
#include "iso_utils.h"
#include "path_table.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

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
    auto view = detail::decodeSectorLayout(raw);
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
    auto view = detail::decodeSectorLayout(m_rawSectorScratch);
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

    if (isCue)
    {
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
