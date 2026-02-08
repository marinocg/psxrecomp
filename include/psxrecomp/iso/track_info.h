#pragma once

#include "psxrecomp/types.h"
#include <string>

namespace psxrecomp
{
namespace iso
{

/**
 * @brief Track type within a CD image.
 */
enum class TrackType
{
    Data,
    Audio,
    Unknown
};

/**
 * @brief Metadata for a single track in a CD image.
 */
struct TrackInfo
{
    u32 trackNumber = 0;
    TrackType type = TrackType::Unknown;
    u32 sectorSize = 0;
    u32 startLba = 0;
    std::string file;
};

} // namespace iso
} // namespace psxrecomp
