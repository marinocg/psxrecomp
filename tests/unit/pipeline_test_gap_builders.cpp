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
