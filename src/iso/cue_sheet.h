#pragma once

#include "psxrecomp/iso/track_info.h"
#include <string>
#include <vector>

namespace psxrecomp
{
namespace iso
{

struct CueSheet
{
    std::vector<TrackInfo> tracks;

    const TrackInfo* findFirstDataTrack() const;
};

bool parseCueSheet(const std::string& cuePath, CueSheet& outSheet, std::string& errorMessage);

} // namespace iso
} // namespace psxrecomp
