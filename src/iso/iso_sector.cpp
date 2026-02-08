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

} // namespace detail
} // namespace iso
} // namespace psxrecomp
