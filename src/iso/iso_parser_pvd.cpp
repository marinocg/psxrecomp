#include "psxrecomp/iso/iso_parser.h"

#include "iso_sector.h"
#include "iso_utils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <optional>
#include <vector>

namespace psxrecomp
{
namespace iso
{

namespace
{

constexpr u32 kUserDataSize = detail::kUserDataSize;
constexpr u32 kRawSectorSize = detail::kRawSectorSize;
constexpr u32 kRawUserDataSectorSize = 2336;
constexpr u32 kRawSubchannelSectorSize = 2448;

struct LayoutProbe
{
    u32 sectorSize = 0;
    u32 userDataOffset = 0;
};

bool hasPvdSignature(const std::array<u8, kUserDataSize>& sector)
{
    return sector[0] == 1 && std::memcmp(sector.data() + 1, "CD001", 5) == 0 && sector[6] == 1;
}

std::vector<LayoutProbe> buildLayoutProbes(u64 fileSize, u32 preferredSectorSize)
{
    std::vector<LayoutProbe> probes;

    auto addProbeIfMissing = [&probes](u32 sectorSize, u32 userDataOffset)
    {
        for (const auto& candidate : probes)
        {
            if (candidate.sectorSize == sectorSize && candidate.userDataOffset == userDataOffset)
            {
                return;
            }
        }
        probes.push_back({sectorSize, userDataOffset});
    };

    if (preferredSectorSize != 0)
    {
        addProbeIfMissing(preferredSectorSize, 0);
    }

    addProbeIfMissing(kUserDataSize, 0);

    if (fileSize % kRawSectorSize == 0)
    {
        addProbeIfMissing(kRawSectorSize, 16);
        addProbeIfMissing(kRawSectorSize, 24);
    }
    if (fileSize % kRawUserDataSectorSize == 0)
    {
        addProbeIfMissing(kRawUserDataSectorSize, 0);
        addProbeIfMissing(kRawUserDataSectorSize, 8);
    }
    if (fileSize % kRawSubchannelSectorSize == 0)
    {
        addProbeIfMissing(kRawSubchannelSectorSize, 16);
        addProbeIfMissing(kRawSubchannelSectorSize, 24);
    }

    addProbeIfMissing(kRawSectorSize, 16);
    addProbeIfMissing(kRawSectorSize, 24);
    addProbeIfMissing(kRawUserDataSectorSize, 0);
    addProbeIfMissing(kRawUserDataSectorSize, 8);
    addProbeIfMissing(kRawSubchannelSectorSize, 16);
    addProbeIfMissing(kRawSubchannelSectorSize, 24);

    return probes;
}

} // namespace

bool IsoParser::readPVD()
{
    std::array<u8, kUserDataSize> sector{};
    std::optional<PrimaryVolumeDescriptor> pvd;
    std::optional<std::pair<u32, u32>> pvdRoot;
    std::optional<std::pair<u32, u32>> jolietRoot;

    m_useJoliet = false;

    std::error_code fileError;
    const auto fileSize = std::filesystem::file_size(m_filename, fileError);
    if (fileError)
    {
        addError("Failed to determine image file size.");
        return false;
    }

    std::vector<u32> candidateTrackStarts;
    if (!m_tracks.empty())
    {
        for (const auto& track : m_tracks)
        {
            if (track.type == TrackType::Data)
            {
                candidateTrackStarts.push_back(track.startLba);
            }
        }
    }
    if (candidateTrackStarts.empty())
    {
        candidateTrackStarts.push_back(m_dataTrackStartLba);
    }
    std::sort(candidateTrackStarts.begin(), candidateTrackStarts.end());
    candidateTrackStarts.erase(
        std::unique(candidateTrackStarts.begin(), candidateTrackStarts.end()),
        candidateTrackStarts.end());

    const auto layoutProbes = buildLayoutProbes(fileSize, m_rawSectorSize);

    for (u32 trackStart : candidateTrackStarts)
    {
        for (const auto& layout : layoutProbes)
        {
            const u64 pvdOffset =
                (static_cast<u64>(trackStart) + 16ULL) * static_cast<u64>(layout.sectorSize) +
                static_cast<u64>(layout.userDataOffset);
            if (layout.sectorSize == 0 || pvdOffset + kUserDataSize > fileSize)
            {
                continue;
            }

            m_stream.clear();
            m_stream.seekg(static_cast<std::streamoff>(pvdOffset), std::ios::beg);
            if (!m_stream.good())
            {
                continue;
            }
            m_stream.read(reinterpret_cast<char*>(sector.data()),
                          static_cast<std::streamsize>(sector.size()));
            if (m_stream.gcount() != static_cast<std::streamsize>(sector.size()) ||
                !hasPvdSignature(sector))
            {
                continue;
            }

            clearSectorCache();
            m_rawSectorSize = layout.sectorSize;
            m_userDataOffset = layout.userDataOffset;
            m_dataTrackStartLba = trackStart;

            pvd.reset();
            pvdRoot.reset();
            jolietRoot.reset();
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
                    candidate.optionalPathTableLba = detail::readLe32(sector.data() + 144);
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

            if (!pvd)
            {
                continue;
            }

            m_pvd = *pvd;
            m_logicalBlockSize = m_pvd.logicalBlockSize;
            const u32 selectedImageSectors = static_cast<u32>(fileSize / m_rawSectorSize);
            if (m_totalSectors == 0)
            {
                m_totalSectors = selectedImageSectors;
            }
            if (fileSize % m_rawSectorSize != 0)
            {
                addError("Image file size is not aligned to sector size.");
            }
            if (m_totalSectors != 0 && m_pvd.volumeSpaceSize > m_totalSectors)
            {
                addError("Volume space size exceeds image size.");
                continue;
            }
            if (!m_tracks.empty() &&
                detail::toUpper(std::filesystem::path(m_inputFilename).extension().string()) !=
                    ".CUE")
            {
                m_tracks.front().sectorSize = m_rawSectorSize;
                m_tracks.front().startLba = m_dataTrackStartLba;
            }
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
                continue;
            }
            return true;
        }
    }

    addError("No valid ISO-9660 PVD found across candidate tracks/layouts.");
    return false;
}

} // namespace iso
} // namespace psxrecomp
