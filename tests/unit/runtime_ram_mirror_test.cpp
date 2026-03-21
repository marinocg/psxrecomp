#include "psxrecomp/runtime/psx_system.h"

#include <stdexcept>

namespace
{
using psxrecomp::u32;
using psxrecomp::runtime::PsxSystem;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
} // namespace

int main()
{
    PsxSystem system;
    require(system.initialize(), "failed to initialize RAM mirror test system");

    system.write<u32>(0x00600020u, 0x11223344u);
    require(system.read<u32>(0x00000020u) == 0x11223344u,
            "8MB RAM mirror did not fold to base RAM");
    require(system.read<u32>(0x80600020u) == 0x11223344u,
            "KSEG0 RAM mirror did not resolve through base RAM");

    system.write<u32>(0x807FFFF0u, 0xAABBCCDDu);
    require(system.read<u32>(0x001FFFF0u) == 0xAABBCCDDu,
            "high mirrored stack address did not reach physical RAM");

    system.write<u32>(0x00800000u, 0x55667788u);
    require(system.read<u32>(0x00000000u) != 0x55667788u,
            "address beyond the first 8MB RAM mirror should not alias RAM");

    return 0;
}
