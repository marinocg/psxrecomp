#pragma once

#include "psxrecomp/types.h"
#include <cstddef>
#include <vector>

namespace psxrecomp
{
namespace iso
{
namespace detail
{

constexpr u32 kUserDataSize = 2048;
constexpr u32 kRawSectorSize = 2352;

struct SectorView
{
    size_t offset = 0;
    size_t size = 0;
};

struct XaSubheader
{
    u8 fileNumber = 0;
    u8 channelNumber = 0;
    u8 submode = 0;
    u8 codingInfo = 0;
};

SectorView decodeSectorLayout(const std::vector<u8>& raw);
bool decodeXaSubheader(const std::vector<u8>& raw, XaSubheader& subheader);
bool isXaAudioSector(const std::vector<u8>& raw, XaSubheader* subheader);

} // namespace detail
} // namespace iso
} // namespace psxrecomp
