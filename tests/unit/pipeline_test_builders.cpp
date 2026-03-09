#include "pipeline_test_builders.h"

#include "psxrecomp/iso/psx_exe_loader.h"

#include <cstring>
#include <string>
#include <vector>

bool hasEmptyBoundaryWarning(const std::vector<std::string>& warnings)
{
    for (const auto& warning : warnings)
    {
        if (warning == "No instructions found for function boundary.")
        {
            return true;
        }
    }
    return false;
}

void writeLe32(std::vector<psxrecomp::u8>& buffer, size_t offset, psxrecomp::u32 value)
{
    buffer[offset] = static_cast<psxrecomp::u8>(value & 0xFF);
    buffer[offset + 1] = static_cast<psxrecomp::u8>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<psxrecomp::u8>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<psxrecomp::u8>((value >> 24) & 0xFF);
}

std::vector<psxrecomp::u8> buildMinimalExe(psxrecomp::u32 loadSize)
{
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    for (psxrecomp::u32 i = 0; i < loadSize; ++i)
    {
        buffer[psxrecomp::iso::PsxExeLoader::kHeaderSize + i] = 0x00;
    }

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithCodeAndAsciiData()
{
    constexpr psxrecomp::u32 loadSize = 64;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0, 0x08004000);  // j 0x80010000
    writeLe32(buffer, codeOffset + 4, 0x00000000);  // nop delay slot
    writeLe32(buffer, codeOffset + 8, 0x6C6C6548);  // 'Hell' (data)
    writeLe32(buffer, codeOffset + 12, 0x6F77206F); // 'o wo' (data)
    writeLe32(buffer, codeOffset + 16, 0xDDDDDDDD); // fill pattern (data)

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithSeparateEntryAndBaseVector()
{
    constexpr psxrecomp::u32 loadSize = 64;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010020); // initial PC (main entry)
    writeLe32(buffer, 0x14, 0x80010000); // GP
    writeLe32(buffer, 0x18, 0x80010000); // load address
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x08004008); // j 0x80010020 (vector trampoline at base)
    writeLe32(buffer, codeOffset + 0x04, 0x00000000); // nop delay slot
    writeLe32(buffer, codeOffset + 0x20, 0x08004008); // j 0x80010020 (main loop)
    writeLe32(buffer, codeOffset + 0x24, 0x00000000); // nop delay slot

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithCopLoadBeforeIndirectJump()
{
    constexpr psxrecomp::u32 loadSize = 64;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x3C088001); // lui t0, 0x8001
    writeLe32(buffer, codeOffset + 0x04, 0x35080020); // ori t0, t0, 0x0020
    writeLe32(buffer, codeOffset + 0x08, 0xC9080000); // lwc2 $8, 0(t0)
    writeLe32(buffer, codeOffset + 0x0C, 0x01000008); // jr t0
    writeLe32(buffer, codeOffset + 0x10, 0x00000000); // nop delay slot
    writeLe32(buffer, codeOffset + 0x20, 0x08004008); // j 0x80010020
    writeLe32(buffer, codeOffset + 0x24, 0x00000000); // nop delay slot

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithStoredDataPointer()
{
    constexpr psxrecomp::u32 loadSize = 64;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x04, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x08, 0x3C028001); // lui v0, 0x8001
    writeLe32(buffer, codeOffset + 0x0C, 0x24420024); // addiu v0, v0, 0x0024
    writeLe32(buffer, codeOffset + 0x10, 0xAFA20008); // sw v0, 8(sp)
    writeLe32(buffer, codeOffset + 0x14, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x18, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x1C, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x20, 0x00000000); // nop delay slot

    // Data bytes chosen to decode as plausible instructions if a false
    // code seed is introduced, which would previously create a bogus
    // second function starting at 0x80010024.
    writeLe32(buffer, codeOffset + 0x24, 0x10000001); // beq zero, zero, +1
    writeLe32(buffer, codeOffset + 0x28, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x2C, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x30, 0x00000000); // nop

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithCallbackPointerPassedToJal()
{
    constexpr psxrecomp::u32 loadSize = 96;
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
    writeLe32(buffer, codeOffset + 0x0C, 0x24840040); // addiu a0, a0, 0x0040
    writeLe32(buffer, codeOffset + 0x10, 0x0C004008); // jal 0x80010020
    writeLe32(buffer, codeOffset + 0x14, 0x00000000); // nop delay slot
    writeLe32(buffer, codeOffset + 0x18, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x1C, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x20, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x24, 0x00000000); // nop

    // Callback registration helper that stores the pointer argument for later use.
    writeLe32(buffer, codeOffset + 0x28, 0x3C088001); // lui t0, 0x8001
    writeLe32(buffer, codeOffset + 0x2C, 0x25080050); // addiu t0, t0, 0x0050
    writeLe32(buffer, codeOffset + 0x30, 0xAD040000); // sw a0, 0(t0)
    writeLe32(buffer, codeOffset + 0x34, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x38, 0x00000000); // nop

    writeLe32(buffer, codeOffset + 0x40, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x44, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x48, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x4C, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x50, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x54, 0x00000000); // nop

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithLiteralDataPointer()
{
    constexpr psxrecomp::u32 loadSize = 80;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x08004000); // j 0x80010000
    writeLe32(buffer, codeOffset + 0x04, 0x00000000); // nop

    // Literal pointer in data to a later data location. The target bytes
    // decode as valid instructions but are not a function entry and must
    // not become reachable code via harvestedPointers.
    writeLe32(buffer, codeOffset + 0x08, 0x80010030);
    writeLe32(buffer, codeOffset + 0x0C, 0x11111111);
    writeLe32(buffer, codeOffset + 0x10, 0x22222222);
    writeLe32(buffer, codeOffset + 0x14, 0x33333333);

    writeLe32(buffer, codeOffset + 0x30, 0x00000000); // sll zero, zero, 0
    writeLe32(buffer, codeOffset + 0x34, 0x10000001); // beq zero, zero, +1
    writeLe32(buffer, codeOffset + 0x38, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x3C, 0x03E00008); // jr ra

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithLiteralFunctionPointerInData()
{
    constexpr psxrecomp::u32 loadSize = 96;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x08004000); // j 0x80010000
    writeLe32(buffer, codeOffset + 0x04, 0x00000000); // nop

    // Data-only pointer to a helper function that is never directly called.
    writeLe32(buffer, codeOffset + 0x08, 0x80010030);
    writeLe32(buffer, codeOffset + 0x0C, 0xAAAAAAAA);
    writeLe32(buffer, codeOffset + 0x10, 0xBBBBBBBB);
    writeLe32(buffer, codeOffset + 0x14, 0xCCCCCCCC);

    writeLe32(buffer, codeOffset + 0x30, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x34, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x38, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x3C, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x40, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x44, 0x00000000); // nop

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithDetachedSingletonFunctionPointer()
{
    constexpr psxrecomp::u32 loadSize = 192;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x08004000); // j 0x80010000
    writeLe32(buffer, codeOffset + 0x04, 0x00000000); // nop

    // Single pointer to a detached helper whose code is separated from the
    // entrypoint by a long blob of data. The helper must still be harvested.
    writeLe32(buffer, codeOffset + 0x08, 0x80010080);
    writeLe32(buffer, codeOffset + 0x0C, 0x11111111);
    writeLe32(buffer, codeOffset + 0x10, 0x22222222);
    writeLe32(buffer, codeOffset + 0x14, 0x33333333);
    writeLe32(buffer, codeOffset + 0x18, 0x44444444);

    // Detached helper target.
    writeLe32(buffer, codeOffset + 0x80, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x84, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x88, 0x2402002A); // li v0, 42
    writeLe32(buffer, codeOffset + 0x8C, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x90, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x94, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x98, 0x00000000); // nop

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithDetachedSingletonResumeLabelPointer()
{
    constexpr psxrecomp::u32 loadSize = 192;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x08004010); // j 0x80010040
    writeLe32(buffer, codeOffset + 0x04, 0x00000000); // nop

    // Single pointer to a detached internal resume label. This mirrors the
    // harvest regression where a lone code pointer targeted a non-prologue
    // detached block that still needed to stay in generated code.
    writeLe32(buffer, codeOffset + 0x08, 0x80010080);
    writeLe32(buffer, codeOffset + 0x0C, 0x11111111);
    writeLe32(buffer, codeOffset + 0x10, 0x22222222);
    writeLe32(buffer, codeOffset + 0x14, 0x33333333);

    writeLe32(buffer, codeOffset + 0x40, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x44, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x48, 0x08004020); // j 0x80010080
    writeLe32(buffer, codeOffset + 0x4C, 0x00000000); // nop

    // Detached non-prologue resume label.
    writeLe32(buffer, codeOffset + 0x80, 0x2402002A); // li v0, 42
    writeLe32(buffer, codeOffset + 0x84, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x88, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x8C, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x90, 0x00000000); // nop

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithClusteredCodePointersInData()
{
    constexpr psxrecomp::u32 loadSize = 112;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x08004000); // j 0x80010000
    writeLe32(buffer, codeOffset + 0x04, 0x00000000); // nop

    // Vtable-like cluster: one canonical function entry plus one internal code
    // label that is a valid resume point but does not start with a prologue.
    writeLe32(buffer, codeOffset + 0x08, 0x8001003C);
    writeLe32(buffer, codeOffset + 0x0C, 0x80010030);
    writeLe32(buffer, codeOffset + 0x10, 0x11111111);
    writeLe32(buffer, codeOffset + 0x14, 0x22222222);

    writeLe32(buffer, codeOffset + 0x30, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x34, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x38, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x3C, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x40, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x44, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x48, 0x00000000); // nop

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithMixedCodeAndStringPointerTable()
{
    constexpr psxrecomp::u32 loadSize = 160;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x08004000); // j 0x80010000
    writeLe32(buffer, codeOffset + 0x04, 0x00000000); // nop

    // Pointer table with one far string target and two real code targets.
    writeLe32(buffer, codeOffset + 0x08, 0x80010080);
    writeLe32(buffer, codeOffset + 0x0C, 0x80010040);
    writeLe32(buffer, codeOffset + 0x10, 0x80010050);

    writeLe32(buffer, codeOffset + 0x40, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x44, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x48, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x4C, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x50, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x54, 0x00000000); // nop

    // Far string payload that decodes as non-code words.
    writeLe32(buffer, codeOffset + 0x80, 0x6C6C6548); // Hell
    writeLe32(buffer, codeOffset + 0x84, 0x6F77206F); // o wo
    writeLe32(buffer, codeOffset + 0x88, 0x00000000);

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithClusteredPointersToDataTables()
{
    constexpr psxrecomp::u32 loadSize = 192;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x08004000); // j 0x80010000
    writeLe32(buffer, codeOffset + 0x04, 0x00000000); // nop

    // Clustered pointers to numeric data tables whose first words decode as
    // unsupported/invalid opcodes and must not be harvested as functions.
    writeLe32(buffer, codeOffset + 0x08, 0x80010080);
    writeLe32(buffer, codeOffset + 0x0C, 0x80010090);
    writeLe32(buffer, codeOffset + 0x10, 0x800100A0);

    writeLe32(buffer, codeOffset + 0x80, 0x00000FFF);
    writeLe32(buffer, codeOffset + 0x84, 0x00000000);
    writeLe32(buffer, codeOffset + 0x88, 0xFFFFFFFF);
    writeLe32(buffer, codeOffset + 0x90, 0x00000001);
    writeLe32(buffer, codeOffset + 0x94, 0x00000001);
    writeLe32(buffer, codeOffset + 0x98, 0x00000001);
    writeLe32(buffer, codeOffset + 0xA0, 0x00000FFF);
    writeLe32(buffer, codeOffset + 0xA4, 0x000007FF);
    writeLe32(buffer, codeOffset + 0xA8, 0x00000333);

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithLocalJumpTableTargets()
{
    constexpr psxrecomp::u32 loadSize = 128;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x3C088001); // lui t0, 0x8001
    writeLe32(buffer, codeOffset + 0x04, 0x25080040); // addiu t0, t0, 0x0040
    writeLe32(buffer, codeOffset + 0x08, 0x00044880); // sll t1, a0, 2
    writeLe32(buffer, codeOffset + 0x0C, 0x01094021); // addu t0, t0, t1
    writeLe32(buffer, codeOffset + 0x10, 0x8D020000); // lw v0, 0(t0)
    writeLe32(buffer, codeOffset + 0x14, 0x00400008); // jr v0
    writeLe32(buffer, codeOffset + 0x18, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x1C, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x20, 0x00000000); // nop

    writeLe32(buffer, codeOffset + 0x40, 0x80010060); // jump table case 0
    writeLe32(buffer, codeOffset + 0x44, 0x8001006C); // jump table case 1
    writeLe32(buffer, codeOffset + 0x48, 0xDEADBEEF); // sentinel, not a code pointer

    writeLe32(buffer, codeOffset + 0x60, 0x24020001); // li v0, 1
    writeLe32(buffer, codeOffset + 0x64, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x68, 0x00000000); // nop
    writeLe32(buffer, codeOffset + 0x6C, 0x24020002); // li v0, 2
    writeLe32(buffer, codeOffset + 0x70, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x74, 0x00000000); // nop

    return buffer;
}

std::vector<psxrecomp::u8> buildExeWithCodeBuiltCallbackTargetAfterPrefixLoads()
{
    constexpr psxrecomp::u32 loadSize = 128;
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);
    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80010000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, loadSize);

    const size_t codeOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize;
    writeLe32(buffer, codeOffset + 0x00, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x04, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x08, 0x3C028001); // lui v0, 0x8001
    writeLe32(buffer, codeOffset + 0x0C, 0x24420040); // addiu v0, v0, 0x0040
    writeLe32(buffer, codeOffset + 0x10, 0xAFA20008); // sw v0, 8(sp)
    writeLe32(buffer, codeOffset + 0x14, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x18, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x1C, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x20, 0x00000000); // nop

    // Target begins with a couple of global loads before the actual stack frame.
    writeLe32(buffer, codeOffset + 0x40, 0x3C028001); // lui v0, 0x8001
    writeLe32(buffer, codeOffset + 0x44, 0x8C420070); // lw v0, 0x0070(v0)
    writeLe32(buffer, codeOffset + 0x48, 0x27BDFFF0); // addiu sp, sp, -16
    writeLe32(buffer, codeOffset + 0x4C, 0xAFBF000C); // sw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x50, 0x8FBF000C); // lw ra, 12(sp)
    writeLe32(buffer, codeOffset + 0x54, 0x27BD0010); // addiu sp, sp, 16
    writeLe32(buffer, codeOffset + 0x58, 0x03E00008); // jr ra
    writeLe32(buffer, codeOffset + 0x5C, 0x00000000); // nop

    return buffer;
}
