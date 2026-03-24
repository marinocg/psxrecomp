#include "pipeline_test_builders.h"

#include "psxrecomp/iso/psx_exe_loader.h"

#include <cstring>
#include <vector>

std::vector<psxrecomp::u8> buildExeWithGapAdjacentRegisterCallTarget()
{
    constexpr psxrecomp::u32 loadSize = 192;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x04, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x08, 0x0C004021); // jal 0x80010084
    writeLe32(buffer, codeOffset + 0x0C, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x18, 0x3C028001); // lui v0, 0x8001
    writeLe32(buffer, codeOffset + 0x1C, 0x24420038); // addiu v0, v0, 0x0038
    writeLe32(buffer, codeOffset + 0x20, 0x0040F809); // jalr ra, v0
    writeLe32(buffer, codeOffset + 0x24, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x28, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x2C, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x30, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x34, 0x00000000); // nop

    // Gap-adjacent callable target starting immediately after the known end.
    writeLe32(buffer, codeOffset + 0x38, 0x00851021); // addu v0, a0, a1
    writeLe32(buffer, codeOffset + 0x3C, 0x24420001); // addiu v0, v0, 1
    writeLe32(buffer, codeOffset + 0x40, 0x304300FF); // andi v1, v0, 0x00ff
    writeLe32(buffer, codeOffset + 0x44, 0x00031840); // sll v1, v1, 1
    writeLe32(buffer, codeOffset + 0x48, 0x00431021); // addu v0, v0, v1
    writeLe32(buffer, codeOffset + 0x4C, 0x34440010); // ori a0, v0, 0x0010
    writeLe32(buffer, codeOffset + 0x50, 0x38840001); // xori a0, a0, 0x0001
    writeLe32(buffer, codeOffset + 0x54, 0x24850002); // addiu a1, a0, 2
    writeLe32(buffer, codeOffset + 0x58, 0x28A3000A); // slti v1, a1, 10
    writeLe32(buffer, codeOffset + 0x5C, 0x00A31021); // addu v0, a1, v1
    writeLe32(buffer, codeOffset + 0x60, 0x34420020); // ori v0, v0, 0x0020
    writeLe32(buffer, codeOffset + 0x64, 0x00401021); // addu v0, v0, zero
    writeLe32(buffer, codeOffset + 0x68, 0x24420003); // addiu v0, v0, 3
    writeLe32(buffer, codeOffset + 0x6C, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x70, 0x00000000); // nop

    // Next known function anchors the far side of the executable gap.
    writeLe32(buffer, codeOffset + 0x80, 0x24020002); // li v0, 2
    writeLe32(buffer, codeOffset + 0x84, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x88, 0x00000000); // nop

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithGapAdjacentPointerCellTarget()
{
    constexpr psxrecomp::u32 loadSize = 192;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x04, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x08, 0x0C004020); // jal 0x80010080
    writeLe32(buffer, codeOffset + 0x0C, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x10, 0x3C028001); // lui v0, 0x8001
    writeLe32(buffer, codeOffset + 0x14, 0x8C420078); // lw v0, 0x0078(v0)
    writeLe32(buffer, codeOffset + 0x18, 0x0040F809); // jalr ra, v0
    writeLe32(buffer, codeOffset + 0x1C, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x20, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x24, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x28, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x2C, 0x00000000); // nop

    writeLe32(buffer, codeOffset + 0x38, 0x00851021); // addu v0, a0, a1
    writeLe32(buffer, codeOffset + 0x3C, 0x24420001); // addiu v0, v0, 1
    writeLe32(buffer, codeOffset + 0x40, 0x304300FF); // andi v1, v0, 0x00ff
    writeLe32(buffer, codeOffset + 0x44, 0x00031840); // sll v1, v1, 1
    writeLe32(buffer, codeOffset + 0x48, 0x00431021); // addu v0, v0, v1
    writeLe32(buffer, codeOffset + 0x4C, 0x34440010); // ori a0, v0, 0x0010
    writeLe32(buffer, codeOffset + 0x50, 0x38840001); // xori a0, a0, 0x0001
    writeLe32(buffer, codeOffset + 0x54, 0x24850002); // addiu a1, a0, 2
    writeLe32(buffer, codeOffset + 0x58, 0x28A3000A); // slti v1, a1, 10
    writeLe32(buffer, codeOffset + 0x5C, 0x00A31021); // addu v0, a1, v1
    writeLe32(buffer, codeOffset + 0x60, 0x34420020); // ori v0, v0, 0x0020
    writeLe32(buffer, codeOffset + 0x64, 0x00401021); // addu v0, v0, zero
    writeLe32(buffer, codeOffset + 0x68, 0x24420003); // addiu v0, v0, 3
    writeLe32(buffer, codeOffset + 0x6C, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x70, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x78, 0x80010038); // pointer cell -> gap-adjacent target
    writeLe32(buffer, codeOffset + 0x7C, 0x00000000); // padding

    writeLe32(buffer, codeOffset + 0x84, 0x24020002); // li v0, 2
    writeLe32(buffer, codeOffset + 0x88, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x8C, 0x00000000); // nop

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithStoredGapAdjacentDispatchTarget()
{
    constexpr psxrecomp::u32 loadSize = 208;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x04, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x08, 0x3C018001); // lui at, 0x8001
    writeLe32(buffer, codeOffset + 0x0C, 0x3C028001); // lui v0, 0x8001
    writeLe32(buffer, codeOffset + 0x10, 0x24420040); // addiu v0, v0, 0x0040
    writeLe32(buffer, codeOffset + 0x14, 0xAC220088); // sw v0, 0x0088(at)
    writeLe32(buffer, codeOffset + 0x18, 0x0C004028); // jal 0x800100A0
    writeLe32(buffer, codeOffset + 0x1C, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x20, 0x3C028001); // lui v0, 0x8001
    writeLe32(buffer, codeOffset + 0x24, 0x8C220088); // lw v0, 0x0088(v0)
    writeLe32(buffer, codeOffset + 0x28, 0x0040F809); // jalr ra, v0
    writeLe32(buffer, codeOffset + 0x2C, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x30, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x34, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x38, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x3C, 0x00000000); // nop

    writeLe32(buffer, codeOffset + 0x40, 0x00851021); // addu v0, a0, a1
    writeLe32(buffer, codeOffset + 0x44, 0x24420001); // addiu v0, v0, 1
    writeLe32(buffer, codeOffset + 0x48, 0x304300FF); // andi v1, v0, 0x00ff
    writeLe32(buffer, codeOffset + 0x4C, 0x00031840); // sll v1, v1, 1
    writeLe32(buffer, codeOffset + 0x50, 0x00431021); // addu v0, v0, v1
    writeLe32(buffer, codeOffset + 0x54, 0x34440010); // ori a0, v0, 0x0010
    writeLe32(buffer, codeOffset + 0x58, 0x38840001); // xori a0, a0, 0x0001
    writeLe32(buffer, codeOffset + 0x5C, 0x24850002); // addiu a1, a0, 2
    writeLe32(buffer, codeOffset + 0x60, 0x28A3000A); // slti v1, a1, 10
    writeLe32(buffer, codeOffset + 0x64, 0x00A31021); // addu v0, a1, v1
    writeLe32(buffer, codeOffset + 0x68, 0x34420020); // ori v0, v0, 0x0020
    writeLe32(buffer, codeOffset + 0x6C, 0x00401021); // addu v0, v0, zero
    writeLe32(buffer, codeOffset + 0x70, 0x24420003); // addiu v0, v0, 3
    writeLe32(buffer, codeOffset + 0x74, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x78, 0x00000000); // nop

    writeLe32(buffer, codeOffset + 0x88, 0x00000000); // dispatch slot initialized at runtime
    writeLe32(buffer, codeOffset + 0x8C, 0x00000000); // padding

    writeLe32(buffer, codeOffset + 0xA0, 0x24020002); // li v0, 2
    writeLe32(buffer, codeOffset + 0xA4, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0xA8, 0x00000000); // nop

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithDelaySlotStoredDispatchTarget()
{
    constexpr psxrecomp::u32 loadSize = 224;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x04, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x08, 0x3C048001); // lui a0, 0x8001
    writeLe32(buffer, codeOffset + 0x0C, 0x0C004028); // jal 0x800100A0
    writeLe32(buffer, codeOffset + 0x10, 0x24840040); // addiu a0, a0, 0x0040
    writeLe32(buffer, codeOffset + 0x14, 0x3C028001); // lui v0, 0x8001
    writeLe32(buffer, codeOffset + 0x18, 0x8C220088); // lw v0, 0x0088(v0)
    writeLe32(buffer, codeOffset + 0x1C, 0x0040F809); // jalr ra, v0
    writeLe32(buffer, codeOffset + 0x20, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x24, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x28, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x2C, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x30, 0x00000000); // nop

    writeLe32(buffer, codeOffset + 0x40, 0x00851021); // addu v0, a0, a1
    writeLe32(buffer, codeOffset + 0x44, 0x24420001); // addiu v0, v0, 1
    writeLe32(buffer, codeOffset + 0x48, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x4C, 0x00000000); // nop

    writeLe32(buffer, codeOffset + 0x88, 0x00000000); // dispatch slot initialized at runtime

    writeLe32(buffer, codeOffset + 0xA0, 0x3C018001); // lui at, 0x8001
    writeLe32(buffer, codeOffset + 0xA4, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0xA8, 0xAC240088); // sw a0, 0x0088(at)

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithReturnedDispatchTargetStore()
{
    constexpr psxrecomp::u32 loadSize = 256;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x04, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x08, 0x0C004028); // jal 0x800100A0
    writeLe32(buffer, codeOffset + 0x0C, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x10, 0x3C048001); // lui a0, 0x8001
    writeLe32(buffer, codeOffset + 0x14, 0x0C004030); // jal 0x800100C0
    writeLe32(buffer, codeOffset + 0x18, 0xAC820088); // sw v0, 0x0088(a0)
    writeLe32(buffer, codeOffset + 0x1C, 0x3C028001); // lui v0, 0x8001
    writeLe32(buffer, codeOffset + 0x20, 0x8C220088); // lw v0, 0x0088(v0)
    writeLe32(buffer, codeOffset + 0x24, 0x0040F809); // jalr ra, v0
    writeLe32(buffer, codeOffset + 0x28, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x2C, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x30, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x34, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x38, 0x00000000); // nop

    writeLe32(buffer, codeOffset + 0x40, 0x00851021); // addu v0, a0, a1
    writeLe32(buffer, codeOffset + 0x44, 0x24420001); // addiu v0, v0, 1
    writeLe32(buffer, codeOffset + 0x48, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x4C, 0x00000000); // nop

    writeLe32(buffer, codeOffset + 0x88, 0x00000000); // dispatch slot initialized at runtime

    writeLe32(buffer, codeOffset + 0xA0, 0x3C028001); // lui v0, 0x8001
    writeLe32(buffer, codeOffset + 0xA4, 0x24420040); // addiu v0, v0, 0x0040
    writeLe32(buffer, codeOffset + 0xA8, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0xAC, 0x00000000); // nop

    writeLe32(buffer, codeOffset + 0xC0, 0x24020002); // li v0, 2
    writeLe32(buffer, codeOffset + 0xC4, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0xC8, 0x00000000); // nop

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithHarvestedDelaySlotSeed()
{
    // A JAL at 0x8001000C has its delay slot at 0x80010010. A pointer
    // table loaded by code stores 0x80010010 twice, making it a
    // highly-scored speculative harvest seed.  The pipeline must not
    // treat the delay-slot address as a function entry.
    auto buffer = buildMinimalExe(0x30);
    const size_t c = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, c + 0x00, 0x3C088001); // lui t0, 0x8001
    writeLe32(buffer, c + 0x04, 0x25080020); // addiu t0, t0, 0x20 => ptr table @0x80010020
    writeLe32(buffer, c + 0x08, 0x8D010000); // lw at, 0(t0)
    writeLe32(buffer, c + 0x0C, 0x0C00400A); // jal 0x80010028
    writeLe32(buffer, c + 0x10, 0x00000000); // nop (delay slot of jal -- must NOT be a function)
    writeLe32(buffer, c + 0x14, 0x03E00008); // jr ra
    writeLe32(buffer, c + 0x18, 0x00000000); // nop
    // pointer table at 0x80010020 -- both entries point to the delay-slot address
    writeLe32(buffer, c + 0x20, 0x80010010); // ptr[0] -> delay-slot address
    writeLe32(buffer, c + 0x24, 0x80010010); // ptr[1] -> sibling
    // callee at 0x80010028
    writeLe32(buffer, c + 0x28, 0x03E00008); // jr ra
    writeLe32(buffer, c + 0x2C, 0x00000000); // nop
    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithBgezalDelaySlotBoundary()
{
    // BGEZAL at 0x80010000 has a delay slot at 0x80010004 that matches a
    // prologue pattern (addiu sp,-32 + sw ra), causing findFunctionBoundaries
    // to split at 0x80010004.  Without the delay-slot boundary extension the
    // gap-fill creates a boundary at 0x80010004, omitting the BGEZAL delay slot
    // from Function A's IR window.
    auto buffer = buildMinimalExe(0x1C);
    const size_t c = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, c + 0x00, 0x04110004); // bgezal zero, +4 => target 0x80010014 (funcB)
    writeLe32(buffer, c + 0x04, 0x27BDFFE0); // addiu sp, sp, -32 (delay slot; prologue match)
    writeLe32(buffer, c + 0x08, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, c + 0x0C, 0x03E00008); // jr ra
    writeLe32(buffer, c + 0x10, 0x00000000); // nop
    // funcB at 0x80010014
    writeLe32(buffer, c + 0x14, 0x03E00008); // jr ra
    writeLe32(buffer, c + 0x18, 0x00000000); // nop
    return buffer;
}
