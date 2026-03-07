#include "psxrecomp/runtime/gte.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <cstdint>

namespace
{
constexpr psxrecomp::u32 packHalfWords(psxrecomp::s16 lo, psxrecomp::s16 hi)
{
    return static_cast<psxrecomp::u32>(static_cast<psxrecomp::u16>(lo)) |
           (static_cast<psxrecomp::u32>(static_cast<psxrecomp::u16>(hi)) << 16);
}

constexpr psxrecomp::u32 packRgbc(psxrecomp::u8 r, psxrecomp::u8 g, psxrecomp::u8 b,
                                  psxrecomp::u8 code)
{
    return static_cast<psxrecomp::u32>(r) | (static_cast<psxrecomp::u32>(g) << 8) |
           (static_cast<psxrecomp::u32>(b) << 16) | (static_cast<psxrecomp::u32>(code) << 24);
}

constexpr psxrecomp::s16 lowHalfSigned(psxrecomp::u32 value)
{
    return static_cast<psxrecomp::s16>(value & 0xFFFFu);
}

constexpr psxrecomp::s16 highHalfSigned(psxrecomp::u32 value)
{
    return static_cast<psxrecomp::s16>((value >> 16) & 0xFFFFu);
}

constexpr psxrecomp::u32 encodeGteCommand(psxrecomp::u32 bits)
{
    return 0x4A000000u | (bits & 0x01FFFFFFu);
}

void loadIdentityRotation(psxrecomp::runtime::Gte& gte)
{
    gte.ctc2(0, packHalfWords(4096, 0));
    gte.ctc2(1, packHalfWords(0, 0));
    gte.ctc2(2, packHalfWords(4096, 0));
    gte.ctc2(3, packHalfWords(0, 0));
    gte.ctc2(4, 4096);
}

void loadIdentityLight(psxrecomp::runtime::Gte& gte)
{
    gte.ctc2(8, packHalfWords(4096, 0));
    gte.ctc2(9, packHalfWords(0, 0));
    gte.ctc2(10, packHalfWords(4096, 0));
    gte.ctc2(11, packHalfWords(0, 0));
    gte.ctc2(12, 4096);
}

void loadIdentityColor(psxrecomp::runtime::Gte& gte)
{
    gte.ctc2(16, packHalfWords(4096, 0));
    gte.ctc2(17, packHalfWords(0, 0));
    gte.ctc2(18, packHalfWords(4096, 0));
    gte.ctc2(19, packHalfWords(0, 0));
    gte.ctc2(20, 4096);
}
} // namespace

int main()
{
    using psxrecomp::runtime::Gte;
    using psxrecomp::runtime::PsxSystem;

    {
        Gte gte;
        gte.reset();

        gte.mtc2(24, 0x12345678u);
        assert(gte.mfc2(24) == 0x12345678u);
        assert(gte.cfc2(24) == 0u);

        gte.ctc2(24, 0x89ABCDEFu);
        assert(gte.cfc2(24) == 0x89ABCDEFu);
        assert(gte.mfc2(24) == 0x12345678u);

        gte.reset();
        assert(gte.mfc2(24) == 0u);
        assert(gte.cfc2(24) == 0u);
    }

    {
        Gte gte;
        gte.reset();
        loadIdentityRotation(gte);
        gte.ctc2(24, 160u << 16);
        gte.ctc2(25, 120u << 16);
        gte.ctc2(26, 320u);
        gte.ctc2(27, 0u);
        gte.ctc2(28, 0u);

        gte.mtc2(0, packHalfWords(100, 50));
        gte.mtc2(1, 1000u);
        gte.exec(encodeGteCommand(0x180001u));

        assert((gte.cfc2(31) & 0x7FFFFFFFu) == 0u);
        assert(gte.mfc2(9) == 100u);
        assert(gte.mfc2(10) == 50u);
        assert(gte.mfc2(11) == 1000u);
        assert(gte.mfc2(19) == 1000u);

        const psxrecomp::u32 sxy2 = gte.mfc2(14);
        assert(lowHalfSigned(sxy2) == 192);
        assert(highHalfSigned(sxy2) == 136);
        assert(gte.mfc2(15) == sxy2);
    }

    {
        Gte gte;
        gte.reset();
        loadIdentityRotation(gte);
        gte.ctc2(24, 160u << 16);
        gte.ctc2(25, 120u << 16);
        gte.ctc2(26, 320u);
        gte.ctc2(27, 0u);
        gte.ctc2(28, 0u);
        gte.ctc2(29, 1365u);
        gte.ctc2(30, 1024u);

        gte.mtc2(0, packHalfWords(100, 50));
        gte.mtc2(1, 1000u);
        gte.mtc2(2, packHalfWords(-100, 50));
        gte.mtc2(3, 1000u);
        gte.mtc2(4, packHalfWords(0, -50));
        gte.mtc2(5, 1000u);

        gte.exec(encodeGteCommand(0x280030u));

        assert(gte.mfc2(16) == 0u);
        assert(gte.mfc2(17) == 1000u);
        assert(gte.mfc2(18) == 1000u);
        assert(gte.mfc2(19) == 1000u);

        const psxrecomp::u32 sxy0 = gte.mfc2(12);
        const psxrecomp::u32 sxy1 = gte.mfc2(13);
        const psxrecomp::u32 sxy2 = gte.mfc2(14);
        assert(lowHalfSigned(sxy0) == 192);
        assert(highHalfSigned(sxy0) == 136);
        assert(lowHalfSigned(sxy1) == 127);
        assert(highHalfSigned(sxy1) == 136);
        assert(lowHalfSigned(sxy2) == 160);
        assert(highHalfSigned(sxy2) == 103);

        gte.exec(encodeGteCommand(0x000006u));
        assert(static_cast<psxrecomp::s32>(gte.mfc2(24)) == 2145);

        gte.exec(encodeGteCommand(0x00002Du));
        assert(gte.mfc2(7) == 999u);

        gte.mtc2(16, 1000u);
        gte.mtc2(17, 1100u);
        gte.mtc2(18, 1200u);
        gte.mtc2(19, 1300u);
        gte.exec(encodeGteCommand(0x00002Eu));
        assert(gte.mfc2(7) == 1150u);
    }

    {
        Gte gte;
        gte.reset();
        loadIdentityLight(gte);
        gte.ctc2(13, 100u);
        gte.ctc2(14, 200u);
        gte.ctc2(15, 300u);
        gte.mtc2(4, packHalfWords(10, 20));
        gte.mtc2(5, 30u);

        gte.exec(encodeGteCommand(0x0B2012u));

        assert(gte.mfc2(9) == 110u);
        assert(gte.mfc2(10) == 220u);
        assert(gte.mfc2(11) == 330u);
        assert(static_cast<psxrecomp::s32>(gte.mfc2(25)) == 110);
        assert(static_cast<psxrecomp::s32>(gte.mfc2(26)) == 220);
        assert(static_cast<psxrecomp::s32>(gte.mfc2(27)) == 330);
        assert((gte.cfc2(31) & 0x7FFFFFFFu) == 0u);
    }

    {
        Gte gte;
        gte.reset();
        loadIdentityRotation(gte);
        gte.mtc2(0, packHalfWords(-10, 20));
        gte.mtc2(1, static_cast<psxrecomp::u32>(static_cast<int32_t>(-30)));

        gte.exec(encodeGteCommand(0x86412u));

        assert(gte.mfc2(9) == 0u);
        assert(gte.mfc2(10) == 20u);
        assert(gte.mfc2(11) == 0u);
        const psxrecomp::u32 flag = gte.cfc2(31);
        assert((flag & (1u << 24)) != 0u);
        assert((flag & (1u << 22)) != 0u);
        assert((flag & (1u << 31)) != 0u);
    }

    {
        Gte gte;
        gte.reset();
        gte.mtc2(6, packRgbc(0x10, 0x20, 0x30, 0x2C));
        gte.mtc2(8, 0u);

        gte.exec(encodeGteCommand((1u << 19) | 0x10u));

        assert(gte.mfc2(20) == 0u);
        assert(gte.mfc2(21) == 0u);
        assert(gte.mfc2(22) == packRgbc(0x10, 0x20, 0x30, 0x2C));
        assert(gte.mfc2(9) == 0x100u);
        assert(gte.mfc2(10) == 0x200u);
        assert(gte.mfc2(11) == 0x300u);
    }

    {
        Gte gte;
        gte.reset();
        loadIdentityLight(gte);
        loadIdentityColor(gte);
        gte.mtc2(6, packRgbc(0, 0, 0, 0x24));
        gte.mtc2(0, packHalfWords(0x400, 0x200));
        gte.mtc2(1, 0x100u);

        gte.exec(encodeGteCommand((1u << 19) | (1u << 10) | 0x1Eu));

        assert(gte.mfc2(22) == packRgbc(0x40, 0x20, 0x10, 0x24));
        assert(gte.mfc2(9) == 0x400u);
        assert(gte.mfc2(10) == 0x200u);
        assert(gte.mfc2(11) == 0x100u);
    }

    {
        Gte gte;
        gte.reset();
        loadIdentityLight(gte);
        loadIdentityColor(gte);
        gte.mtc2(6, packRgbc(0x80, 0x80, 0x80, 0x25));
        gte.mtc2(0, packHalfWords(0x400, 0x200));
        gte.mtc2(1, 0x100u);

        gte.exec(encodeGteCommand((1u << 19) | (1u << 10) | 0x1Bu));

        assert(gte.mfc2(22) == packRgbc(0x20, 0x10, 0x08, 0x25));
        assert(gte.mfc2(9) == 0x200u);
        assert(gte.mfc2(10) == 0x100u);
        assert(gte.mfc2(11) == 0x080u);
    }

    {
        Gte gte;
        gte.reset();
        loadIdentityLight(gte);
        loadIdentityColor(gte);
        gte.ctc2(21, 0x800u);
        gte.ctc2(22, 0x400u);
        gte.ctc2(23, 0x200u);
        gte.mtc2(6, packRgbc(0x80, 0x80, 0x80, 0x26));
        gte.mtc2(8, 0x1000u);
        gte.mtc2(0, packHalfWords(0x400, 0x200));
        gte.mtc2(1, 0x100u);

        gte.exec(encodeGteCommand((1u << 19) | (1u << 10) | 0x13u));

        assert(gte.mfc2(22) == packRgbc(0x80, 0x40, 0x20, 0x26));
        assert(gte.mfc2(9) == 0x800u);
        assert(gte.mfc2(10) == 0x400u);
        assert(gte.mfc2(11) == 0x200u);
    }

    {
        Gte gte;
        gte.reset();
        loadIdentityColor(gte);
        gte.ctc2(21, 0x800u);
        gte.ctc2(22, 0x400u);
        gte.ctc2(23, 0x200u);
        gte.mtc2(6, packRgbc(0x80, 0x80, 0x80, 0x27));
        gte.mtc2(8, 0x1000u);
        gte.mtc2(9, 0x400u);
        gte.mtc2(10, 0x200u);
        gte.mtc2(11, 0x100u);

        gte.exec(encodeGteCommand((1u << 19) | (1u << 10) | 0x14u));

        assert(gte.mfc2(22) == packRgbc(0x80, 0x40, 0x20, 0x27));
        assert(gte.mfc2(9) == 0x800u);
        assert(gte.mfc2(10) == 0x400u);
        assert(gte.mfc2(11) == 0x200u);
    }

    {
        Gte gte;
        gte.reset();
        gte.ctc2(21, 0x800u);
        gte.ctc2(22, 0x400u);
        gte.ctc2(23, 0x200u);
        gte.mtc2(6, packRgbc(0, 0, 0, 0x28));
        gte.mtc2(8, 0x1000u);
        gte.mtc2(9, 0x400u);
        gte.mtc2(10, 0x200u);
        gte.mtc2(11, 0x100u);

        gte.exec(encodeGteCommand((1u << 19) | (1u << 10) | 0x11u));

        assert(gte.mfc2(22) == packRgbc(0x80, 0x40, 0x20, 0x28));
        assert(gte.mfc2(9) == 0x800u);
        assert(gte.mfc2(10) == 0x400u);
        assert(gte.mfc2(11) == 0x200u);
    }

    {
        Gte gte;
        gte.reset();
        gte.ctc2(21, 0x800u);
        gte.ctc2(22, 0x400u);
        gte.ctc2(23, 0x200u);
        gte.mtc2(6, packRgbc(0x80, 0x80, 0x80, 0x29));
        gte.mtc2(8, 0x1000u);
        gte.mtc2(9, 0x400u);
        gte.mtc2(10, 0x200u);
        gte.mtc2(11, 0x100u);

        gte.exec(encodeGteCommand((1u << 19) | (1u << 10) | 0x29u));

        assert(gte.mfc2(22) == packRgbc(0x80, 0x40, 0x20, 0x29));
    }

    {
        Gte gte;
        gte.reset();
        gte.mtc2(6, packRgbc(0, 0, 0, 0x2A));
        gte.mtc2(8, 0x800u);
        gte.mtc2(9, 0x400u);
        gte.mtc2(10, 0x200u);
        gte.mtc2(11, 0x100u);

        gte.exec(encodeGteCommand((1u << 19) | (1u << 10) | 0x3Du));
        assert(gte.mfc2(22) == packRgbc(0x20, 0x10, 0x08, 0x2A));

        gte.mtc2(25, 0x100u);
        gte.mtc2(26, 0x200u);
        gte.mtc2(27, 0x300u);
        gte.exec(encodeGteCommand((1u << 19) | (1u << 10) | 0x3Eu));
        assert(gte.mfc2(22) == packRgbc(0x20, 0x28, 0x34, 0x2A));
    }

    {
        Gte gte;
        gte.reset();
        loadIdentityLight(gte);
        loadIdentityColor(gte);
        gte.mtc2(6, packRgbc(0x80, 0x80, 0x80, 0x30));
        gte.mtc2(8, 0x1000u);
        gte.ctc2(21, 0x800u);
        gte.ctc2(22, 0x400u);
        gte.ctc2(23, 0x200u);

        gte.mtc2(0, packHalfWords(0x100, 0x100));
        gte.mtc2(1, 0x100u);
        gte.mtc2(2, packHalfWords(0x200, 0x300));
        gte.mtc2(3, 0x400u);
        gte.mtc2(4, packHalfWords(0x400, 0x200));
        gte.mtc2(5, 0x100u);

        gte.exec(encodeGteCommand((1u << 19) | (1u << 10) | 0x20u));
        assert(gte.mfc2(20) == packRgbc(0x10, 0x10, 0x10, 0x30));
        assert(gte.mfc2(21) == packRgbc(0x20, 0x30, 0x40, 0x30));
        assert(gte.mfc2(22) == packRgbc(0x40, 0x20, 0x10, 0x30));

        gte.exec(encodeGteCommand((1u << 19) | (1u << 10) | 0x3Fu));
        assert(gte.mfc2(20) == packRgbc(0x08, 0x08, 0x08, 0x30));
        assert(gte.mfc2(21) == packRgbc(0x10, 0x18, 0x20, 0x30));
        assert(gte.mfc2(22) == packRgbc(0x20, 0x10, 0x08, 0x30));

        gte.exec(encodeGteCommand((1u << 19) | (1u << 10) | 0x16u));
        assert(gte.mfc2(20) == packRgbc(0x80, 0x40, 0x20, 0x30));
        assert(gte.mfc2(21) == packRgbc(0x80, 0x40, 0x20, 0x30));
        assert(gte.mfc2(22) == packRgbc(0x80, 0x40, 0x20, 0x30));
    }

    {
        Gte gte;
        gte.reset();
        gte.mtc2(6, packRgbc(0, 0, 0, 0x31));
        gte.mtc2(8, 0u);
        gte.mtc2(20, packRgbc(0x10, 0x20, 0x30, 0x01));
        gte.mtc2(21, packRgbc(0x40, 0x50, 0x60, 0x02));
        gte.mtc2(22, packRgbc(0x70, 0x80, 0x90, 0x03));

        gte.exec(encodeGteCommand((1u << 19) | 0x2Au));

        assert(gte.mfc2(20) == packRgbc(0x10, 0x20, 0x30, 0x31));
        assert(gte.mfc2(21) == packRgbc(0x40, 0x50, 0x60, 0x31));
        assert(gte.mfc2(22) == packRgbc(0x70, 0x80, 0x90, 0x31));
    }

    {
        Gte gte;
        gte.reset();
        gte.ctc2(31, 0xFFFFFFFFu);
        assert(gte.cfc2(31) == 0xFFFFF000u);

        gte.mtc2(30, 0u);
        assert(gte.mfc2(31) == 32u);
        gte.mtc2(30, 1u);
        assert(gte.mfc2(31) == 31u);
        gte.mtc2(30, 0xFFFFFFFFu);
        assert(gte.mfc2(31) == 32u);
    }

    {
        PsxSystem system;
        system.reset();

        system.gte().exec(encodeGteCommand(0x00002Du));
        const uint64_t readStallBefore = system.cpuCyclesElapsed();
        assert(system.gte().mfc2(7) == 0u);
        assert(system.cpuCyclesElapsed() == readStallBefore + 5u);

        system.gte().exec(encodeGteCommand(0x00002Du));
        const uint64_t writeStallBefore = system.cpuCyclesElapsed();
        system.gte().mtc2(7, 0x1234u);
        system.gte().ctc2(24, 0x5678u);
        assert(system.cpuCyclesElapsed() == writeStallBefore);

        system.gte().exec(encodeGteCommand(0x00002Du));
        const uint64_t execStallBefore = system.cpuCyclesElapsed();
        system.gte().exec(encodeGteCommand(0x00002Eu));
        assert(system.cpuCyclesElapsed() == execStallBefore + 5u);
        assert(system.gte().busyCyclesRemaining() == 6u);
    }

    {
        PsxSystem system;
        system.reset();

        system.gte().mtc2(28, 0x7C1Fu);
        assert(system.gte().mfc2(9) == 0u);
        assert(system.gte().mfc2(10) == 0u);
        assert(system.gte().mfc2(11) == 0u);

        const uint64_t irgbReadBefore = system.cpuCyclesElapsed();
        assert(system.gte().mfc2(29) == 0x7C1Fu);
        assert(system.cpuCyclesElapsed() == irgbReadBefore + 3u);
        assert(system.gte().mfc2(9) == 0x0F80u);
        assert(system.gte().mfc2(10) == 0x0000u);
        assert(system.gte().mfc2(11) == 0x0F80u);
    }

    {
        PsxSystem system;
        system.gte().mtc2(24, 0x11223344u);
        system.gte().ctc2(24, 0x55667788u);
        system.reset();
        assert(system.gte().mfc2(24) == 0u);
        assert(system.gte().cfc2(24) == 0u);
    }

    return 0;
}
