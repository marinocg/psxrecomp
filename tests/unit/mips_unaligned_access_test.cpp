#include "psxrecomp/runtime/mips_unaligned_access.h"

#include <array>
#include <cassert>

namespace
{
using psxrecomp::Address;
using psxrecomp::runtime::PsxSystem;

void writeBytes(PsxSystem& system, Address address, const std::array<psxrecomp::u8, 8>& bytes)
{
    for (size_t index = 0; index < bytes.size(); ++index)
    {
        system.write<psxrecomp::u8>(address + static_cast<Address>(index), bytes[index]);
    }
}

psxrecomp::u32 readWord(PsxSystem& system, Address address)
{
    return system.read<psxrecomp::u32>(address);
}
} // namespace

int main()
{
    using namespace psxrecomp;
    using namespace psxrecomp::runtime;

    {
        PsxSystem system;
        assert(system.initialize());
        constexpr Address base = 0x1000u;
        constexpr u32 registerValue = 0xA1B2C3D4u;
        writeBytes(system, base, {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0});

        assert(loadWordLeft(system, base + 0u, registerValue) == 0x11B2C3D4u);
        assert(loadWordLeft(system, base + 1u, registerValue) == 0x2211C3D4u);
        assert(loadWordLeft(system, base + 2u, registerValue) == 0x332211D4u);
        assert(loadWordLeft(system, base + 3u, registerValue) == 0x44332211u);

        assert(loadWordRight(system, base + 0u, registerValue) == 0x44332211u);
        assert(loadWordRight(system, base + 1u, registerValue) == 0xA1443322u);
        assert(loadWordRight(system, base + 2u, registerValue) == 0xA1B24433u);
        assert(loadWordRight(system, base + 3u, registerValue) == 0xA1B2C344u);
    }

    {
        PsxSystem system;
        assert(system.initialize());
        constexpr Address base = 0x1100u;
        constexpr u32 registerValue = 0xA1B2C3D4u;

        writeBytes(system, base, {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0});
        storeWordLeft(system, base + 0u, registerValue);
        assert(readWord(system, base) == 0x443322A1u);

        writeBytes(system, base, {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0});
        storeWordLeft(system, base + 1u, registerValue);
        assert(readWord(system, base) == 0x4433A1B2u);

        writeBytes(system, base, {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0});
        storeWordLeft(system, base + 2u, registerValue);
        assert(readWord(system, base) == 0x44A1B2C3u);

        writeBytes(system, base, {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0});
        storeWordLeft(system, base + 3u, registerValue);
        assert(readWord(system, base) == 0xA1B2C3D4u);

        writeBytes(system, base, {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0});
        storeWordRight(system, base + 0u, registerValue);
        assert(readWord(system, base) == 0xA1B2C3D4u);

        writeBytes(system, base, {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0});
        storeWordRight(system, base + 1u, registerValue);
        assert(readWord(system, base) == 0xB2C3D411u);

        writeBytes(system, base, {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0});
        storeWordRight(system, base + 2u, registerValue);
        assert(readWord(system, base) == 0xC3D42211u);

        writeBytes(system, base, {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0});
        storeWordRight(system, base + 3u, registerValue);
        assert(readWord(system, base) == 0xD4332211u);
    }

    {
        PsxSystem system;
        assert(system.initialize());
        constexpr Address base = 0x1200u;
        constexpr Address unaligned = base + 1u;
        writeBytes(system, base, {0x00, 0x03, 0x45, 0x02, 0x01, 0x01, 0x48, 0x00});

        u32 merged = 0u;
        merged = loadWordLeft(system, unaligned + 3u, merged);
        merged = loadWordRight(system, unaligned + 0u, merged);
        assert(merged == 0x01024503u);
    }

    {
        PsxSystem system;
        assert(system.initialize());
        constexpr Address base = 0x1300u;
        constexpr Address unaligned = base + 1u;
        constexpr u32 value = 0x01024503u;
        writeBytes(system, base, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

        storeWordLeft(system, unaligned + 3u, value);
        storeWordRight(system, unaligned + 0u, value);

        std::array<u8, 5> actual = {};
        for (size_t index = 0; index < actual.size(); ++index)
        {
            actual[index] = system.read<u8>(base + static_cast<Address>(index));
        }
        assert((actual == std::array<u8, 5>{0x00, 0x03, 0x45, 0x02, 0x01}));
    }

    return 0;
}
