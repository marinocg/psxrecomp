#include "psxrecomp/iso/multi_disc_set.h"

namespace psxrecomp
{
namespace iso
{

MultiDiscSet::MultiDiscSet(std::vector<std::string> discPaths)
    : m_parsers(), m_discInfo(), m_activeDiscIndex(0)
{
    m_parsers.reserve(discPaths.size());
    m_discInfo.reserve(discPaths.size());
    for (const auto& path : discPaths)
    {
        m_parsers.push_back(std::make_unique<IsoParser>(path));
        DiscInfo info{};
        info.path = path;
        m_discInfo.push_back(info);
    }
}

bool MultiDiscSet::open()
{
    bool success = true;
    for (size_t i = 0; i < m_parsers.size(); ++i)
    {
        auto& parser = m_parsers[i];
        auto& info = m_discInfo[i];
        if (!parser->open())
        {
            info.isValid = false;
            success = false;
            continue;
        }
        info.isValid = parser->isValid();
        info.volumeLabel = parser->getVolumeLabel();
        info.tracks = parser->getTracks();
        for (auto& track : info.tracks)
        {
            track.discIndex = static_cast<u32>(i);
        }
    }
    return success;
}

size_t MultiDiscSet::getDiscCount() const
{
    return m_discInfo.size();
}

const MultiDiscSet::DiscInfo& MultiDiscSet::getDiscInfo(size_t index) const
{
    return m_discInfo.at(index);
}

bool MultiDiscSet::setActiveDisc(size_t index)
{
    if (index >= m_parsers.size())
    {
        return false;
    }
    m_activeDiscIndex = index;
    return true;
}

size_t MultiDiscSet::getActiveDiscIndex() const
{
    return m_activeDiscIndex;
}

IsoParser* MultiDiscSet::getActiveParser()
{
    if (m_parsers.empty())
    {
        return nullptr;
    }
    return m_parsers[m_activeDiscIndex].get();
}

std::vector<TrackInfo> MultiDiscSet::getCumulativeTracks() const
{
    std::vector<TrackInfo> tracks;
    for (const auto& disc : m_discInfo)
    {
        tracks.insert(tracks.end(), disc.tracks.begin(), disc.tracks.end());
    }
    return tracks;
}

} // namespace iso
} // namespace psxrecomp
