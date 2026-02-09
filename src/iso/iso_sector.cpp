#include "iso_sector.h"

namespace psxrecomp
{
namespace iso
{
namespace detail
{

namespace
{

constexpr u8 kMode1 = 1;
constexpr u8 kMode2 = 2;
constexpr u8 kSubmodeForm2 = 0x20;
constexpr u8 kSubmodeAudio = 0x04;

} // namespace

SectorView decodeSectorLayout(const std::vector<u8>& raw)
{
    if (raw.size() == kUserDataSize)
    {
        return {0, kUserDataSize};
    }
    if (raw.size() < kRawSectorSize)
    {
        return {0, 0};
    }
    u8 mode = raw[15];
    if (mode == kMode1)
    {
        return {16, kUserDataSize};
    }
    if (mode == kMode2)
    {
        u8 submode = raw[18];
        if ((submode & kSubmodeForm2) != 0)
        {
            return {24, 2324};
        }
        return {24, kUserDataSize};
    }
    return {0, 0};
}

bool decodeXaSubheader(const std::vector<u8>& raw, XaSubheader& subheader)
{
    if (raw.size() < kRawSectorSize)
    {
        return false;
    }
    if (raw[15] != kMode2)
    {
        return false;
    }
    subheader.fileNumber = raw[16];
    subheader.channelNumber = raw[17];
    subheader.submode = raw[18];
    subheader.codingInfo = raw[19];
    return true;
}

bool isXaAudioSector(const std::vector<u8>& raw, XaSubheader* subheader)
{
    XaSubheader local{};
    if (!decodeXaSubheader(raw, local))
    {
        return false;
    }
    if ((local.submode & kSubmodeAudio) == 0)
    {
        return false;
    }
    if (subheader)
    {
        *subheader = local;
    }
    return true;
}

} // namespace detail
} // namespace iso
} // namespace psxrecomp
