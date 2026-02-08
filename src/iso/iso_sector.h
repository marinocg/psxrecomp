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

SectorView decodeSectorLayout(const std::vector<u8>& raw);

} // namespace detail
} // namespace iso
} // namespace psxrecomp
