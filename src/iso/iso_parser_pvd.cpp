#include "psxrecomp/iso/iso_parser.h"

#include "iso_sector.h"
#include "iso_utils.h"

#include <array>
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

        if (pvd)
        {
            m_pvd = *pvd;
            m_logicalBlockSize = m_pvd.logicalBlockSize;
            if (m_totalSectors == 0 && m_rawSectorSize != 0)
            {
                std::error_code error;
                auto fileSize = std::filesystem::file_size(m_filename, error);
                if (error)
                {
                    addError("Failed to determine image file size.");
                }
                else
                {
                    m_totalSectors = static_cast<u32>(fileSize / m_rawSectorSize);
                    if (fileSize % m_rawSectorSize != 0)
                    {
                        addError("Image file size is not aligned to sector size.");
                    }
                }
            }
            if (m_totalSectors != 0 && m_pvd.volumeSpaceSize > m_totalSectors)
            {
                addError("Volume space size exceeds image size.");
                return false;
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
                return false;
            }
            return true;
        }
    }

    return false;
}

} // namespace iso
} // namespace psxrecomp
