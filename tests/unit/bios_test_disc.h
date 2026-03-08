#pragma once

#include "psxrecomp/runtime/disc.h"
#include "psxrecomp/types.h"

#include <algorithm>
#include <span>

namespace
{
class TestDisc final : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(psxrecomp::u32, std::span<psxrecomp::u8, 2048> out) override
    {
        std::fill(out.begin(), out.end(), 0);
        return true;
    }

    psxrecomp::u32 userSectorCount() const override
    {
        return 16;
    }
};
} // namespace
