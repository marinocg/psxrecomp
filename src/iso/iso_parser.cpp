#include "psxrecomp/iso/iso_parser.h"

#include "iso_sector.h"

namespace psxrecomp
{
namespace iso
{

namespace
{

constexpr u32 kUserDataSize = detail::kUserDataSize;

} // namespace

IsoParser::IsoParser(const std::string& filename)
    : m_filename(filename), m_inputFilename(filename), m_isOpen(false), m_isValid(false),
      m_rawSectorSize(kUserDataSize), m_dataTrackStartLba(0), m_logicalBlockSize(kUserDataSize),
      m_useJoliet(false), m_stream(), m_rawSectorScratch(), m_pvd{}, m_rootDirectory(),
      m_rootExtent(0), m_rootSize(0), m_totalSectors(0), m_tracks(), m_errors()
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

} // namespace iso
} // namespace psxrecomp
