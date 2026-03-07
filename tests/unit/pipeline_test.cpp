#include "psxrecomp/recompiler/pipeline.h"

#include "psxrecomp/iso/psx_exe_loader.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

namespace
{
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

} // namespace

int main()
{
    auto tempDir = std::filesystem::temp_directory_path();
    auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device randomDevice;
    std::uniform_int_distribution<int> dist(0, 0xFFFF);
    auto suffix = std::to_string(timestamp) + "_" + std::to_string(dist(randomDevice));

    std::filesystem::path exePath = tempDir / ("psxrecomp_pipeline_" + suffix + ".psx");
    std::filesystem::path ecmPath = tempDir / ("psxrecomp_pipeline_" + suffix + ".bin.ecm");
    std::filesystem::path outputDir = tempDir / ("psxrecomp_pipeline_out_" + suffix);

    struct CleanupGuard
    {
        std::vector<std::filesystem::path> exes;
        std::filesystem::path ecm;
        std::filesystem::path out;
        ~CleanupGuard()
        {
            std::error_code error;
            for (const auto& exe : exes)
            {
                std::filesystem::remove(exe, error);
            }
            std::filesystem::remove(ecm, error);
            std::filesystem::remove_all(out, error);
        }
    };
    CleanupGuard guard{{exePath}, ecmPath, outputDir};

    auto buffer = buildMinimalExe(16);
    std::ofstream outFile(exePath, std::ios::binary);
    outFile.write(reinterpret_cast<const char*>(buffer.data()),
                  static_cast<std::streamsize>(buffer.size()));
    outFile.close();
    assert(std::filesystem::exists(exePath));

    psxrecomp::recompiler::PipelineOptions options;
    options.outputDirectory = outputDir.string();
    options.enableOptimizations = false;
    options.preserveSymbols = false;
    options.verbose = false;
    options.manifestTimestamp = "2024-01-01T00:00:00Z";

    psxrecomp::recompiler::RecompilationPipeline pipeline(options);
    auto resultA = pipeline.run(exePath.string());
    assert(resultA.success);
    assert(!resultA.artifacts.headerPath.empty());
    assert(!resultA.artifacts.sourcePath.empty());
    assert(!resultA.artifacts.buildPath.empty());
    assert(!resultA.artifacts.manifestPath.empty());
    assert(!resultA.artifacts.resourcesPath.empty());
    assert(!resultA.artifacts.runtimeIncludePath.empty());
    assert(!resultA.artifacts.runtimeSourcePath.empty());
    assert(std::filesystem::exists(resultA.artifacts.headerPath));
    assert(std::filesystem::exists(resultA.artifacts.sourcePath));
    assert(std::filesystem::exists(resultA.artifacts.buildPath));
    assert(std::filesystem::exists(resultA.artifacts.manifestPath));
    assert(std::filesystem::exists(resultA.artifacts.runtimeIncludePath));
    assert(std::filesystem::exists(resultA.artifacts.runtimeSourcePath));

    auto resultB = pipeline.run(exePath.string());
    assert(resultB.success);
    assert(resultA.artifacts.headerPath == resultB.artifacts.headerPath);
    assert(resultA.artifacts.sourcePath == resultB.artifacts.sourcePath);
    assert(resultA.artifacts.buildPath == resultB.artifacts.buildPath);
    assert(resultA.artifacts.manifestPath == resultB.artifacts.manifestPath);
    assert(resultA.artifacts.runtimeIncludePath == resultB.artifacts.runtimeIncludePath);
    assert(resultA.artifacts.runtimeSourcePath == resultB.artifacts.runtimeSourcePath);

    std::ifstream manifestA(resultA.artifacts.manifestPath);
    std::stringstream manifestBufferA;
    manifestBufferA << manifestA.rdbuf();
    std::string manifestContentA = manifestBufferA.str();

    std::ifstream manifestB(resultB.artifacts.manifestPath);
    std::stringstream manifestBufferB;
    manifestBufferB << manifestB.rdbuf();
    std::string manifestContentB = manifestBufferB.str();

    assert(manifestContentA == manifestContentB);
    assert(manifestContentA.find("\"pipelineVersion\"") != std::string::npos);
    assert(manifestContentA.find("\"timestamp\"") != std::string::npos);
    assert(manifestContentA.find("\"input\"") != std::string::npos);
    assert(manifestContentA.find("\"output\"") != std::string::npos);
    assert(manifestContentA.find("\"runtimeInclude\"") != std::string::npos);
    assert(manifestContentA.find("\"runtimeSource\"") != std::string::npos);

    std::filesystem::path vectorExePath =
        tempDir / ("psxrecomp_pipeline_vector_" + suffix + ".psx");
    guard.exes.push_back(vectorExePath);
    auto vectorBuffer = buildExeWithSeparateEntryAndBaseVector();
    std::ofstream vectorFile(vectorExePath, std::ios::binary);
    vectorFile.write(reinterpret_cast<const char*>(vectorBuffer.data()),
                     static_cast<std::streamsize>(vectorBuffer.size()));
    vectorFile.close();
    auto vectorResult = pipeline.run(vectorExePath.string());
    assert(vectorResult.success);
    assert(!hasEmptyBoundaryWarning(vectorResult.warnings));
    bool hasBaseVectorFunction = false;
    for (const auto& function : vectorResult.functions)
    {
        if (function.entryAddress == 0x80010000)
        {
            hasBaseVectorFunction = true;
            break;
        }
    }
    assert(hasBaseVectorFunction);

    std::filesystem::path copLoadExePath =
        tempDir / ("psxrecomp_pipeline_lwc2_indirect_" + suffix + ".psx");
    guard.exes.push_back(copLoadExePath);
    auto copLoadBuffer = buildExeWithCopLoadBeforeIndirectJump();
    std::ofstream copLoadFile(copLoadExePath, std::ios::binary);
    copLoadFile.write(reinterpret_cast<const char*>(copLoadBuffer.data()),
                      static_cast<std::streamsize>(copLoadBuffer.size()));
    copLoadFile.close();
    auto copLoadResult = pipeline.run(copLoadExePath.string());
    assert(copLoadResult.success);
    assert(!hasEmptyBoundaryWarning(copLoadResult.warnings));
    bool hasIndirectTargetFunction = false;
    for (const auto& function : copLoadResult.functions)
    {
        if (function.entryAddress == 0x80010020)
        {
            hasIndirectTargetFunction = true;
            break;
        }
    }
    assert(hasIndirectTargetFunction);

    std::filesystem::path storedPointerExePath =
        tempDir / ("psxrecomp_pipeline_stored_pointer_" + suffix + ".psx");
    guard.exes.push_back(storedPointerExePath);
    auto storedPointerBuffer = buildExeWithStoredDataPointer();
    std::ofstream storedPointerFile(storedPointerExePath, std::ios::binary);
    storedPointerFile.write(reinterpret_cast<const char*>(storedPointerBuffer.data()),
                            static_cast<std::streamsize>(storedPointerBuffer.size()));
    storedPointerFile.close();
    auto storedPointerResult = pipeline.run(storedPointerExePath.string());
    assert(storedPointerResult.success);
    assert(!hasEmptyBoundaryWarning(storedPointerResult.warnings));
    for (const auto& function : storedPointerResult.functions)
    {
        assert(function.entryAddress != 0x80010024);
    }

    std::filesystem::path literalPointerExePath =
        tempDir / ("psxrecomp_pipeline_literal_pointer_" + suffix + ".psx");
    guard.exes.push_back(literalPointerExePath);
    auto literalPointerBuffer = buildExeWithLiteralDataPointer();
    std::ofstream literalPointerFile(literalPointerExePath, std::ios::binary);
    literalPointerFile.write(reinterpret_cast<const char*>(literalPointerBuffer.data()),
                             static_cast<std::streamsize>(literalPointerBuffer.size()));
    literalPointerFile.close();
    auto literalPointerResult = pipeline.run(literalPointerExePath.string());
    assert(literalPointerResult.success);
    assert(!hasEmptyBoundaryWarning(literalPointerResult.warnings));
    for (const auto& function : literalPointerResult.functions)
    {
        assert(function.entryAddress != 0x80010030);
    }

    std::filesystem::path literalFunctionPointerExePath =
        tempDir / ("psxrecomp_pipeline_literal_function_pointer_" + suffix + ".psx");
    guard.exes.push_back(literalFunctionPointerExePath);
    auto literalFunctionPointerBuffer = buildExeWithLiteralFunctionPointerInData();
    std::ofstream literalFunctionPointerFile(literalFunctionPointerExePath, std::ios::binary);
    literalFunctionPointerFile.write(
        reinterpret_cast<const char*>(literalFunctionPointerBuffer.data()),
        static_cast<std::streamsize>(literalFunctionPointerBuffer.size()));
    literalFunctionPointerFile.close();
    auto literalFunctionPointerResult = pipeline.run(literalFunctionPointerExePath.string());
    assert(literalFunctionPointerResult.success);
    assert(!hasEmptyBoundaryWarning(literalFunctionPointerResult.warnings));
    bool hasLiteralFunctionPointerTarget = false;
    for (const auto& function : literalFunctionPointerResult.functions)
    {
        if (function.entryAddress == 0x80010030)
        {
            hasLiteralFunctionPointerTarget = true;
        }
    }
    assert(hasLiteralFunctionPointerTarget);

    std::filesystem::path clusteredCodePointerExePath =
        tempDir / ("psxrecomp_pipeline_clustered_code_pointer_" + suffix + ".psx");
    guard.exes.push_back(clusteredCodePointerExePath);
    auto clusteredCodePointerBuffer = buildExeWithClusteredCodePointersInData();
    std::ofstream clusteredCodePointerFile(clusteredCodePointerExePath, std::ios::binary);
    clusteredCodePointerFile.write(reinterpret_cast<const char*>(clusteredCodePointerBuffer.data()),
                                   static_cast<std::streamsize>(clusteredCodePointerBuffer.size()));
    clusteredCodePointerFile.close();
    auto clusteredCodePointerResult = pipeline.run(clusteredCodePointerExePath.string());
    assert(clusteredCodePointerResult.success);
    assert(!hasEmptyBoundaryWarning(clusteredCodePointerResult.warnings));
    bool hasClusteredLabelTarget = false;
    for (const auto& function : clusteredCodePointerResult.functions)
    {
        if (function.entryAddress == 0x8001003C)
        {
            hasClusteredLabelTarget = true;
        }
    }
    assert(hasClusteredLabelTarget);

    std::filesystem::path callbackPointerExePath =
        tempDir / ("psxrecomp_pipeline_callback_pointer_" + suffix + ".psx");
    guard.exes.push_back(callbackPointerExePath);
    auto callbackPointerBuffer = buildExeWithCallbackPointerPassedToJal();
    std::ofstream callbackPointerFile(callbackPointerExePath, std::ios::binary);
    callbackPointerFile.write(reinterpret_cast<const char*>(callbackPointerBuffer.data()),
                              static_cast<std::streamsize>(callbackPointerBuffer.size()));
    callbackPointerFile.close();
    auto callbackPointerResult = pipeline.run(callbackPointerExePath.string());
    assert(callbackPointerResult.success);
    assert(!hasEmptyBoundaryWarning(callbackPointerResult.warnings));
    bool hasCallbackTarget = false;
    for (const auto& function : callbackPointerResult.functions)
    {
        if (function.entryAddress == 0x80010040)
        {
            hasCallbackTarget = true;
        }
    }
    assert(hasCallbackTarget);

    std::filesystem::path prefixedCallbackExePath =
        tempDir / ("psxrecomp_pipeline_prefixed_callback_target_" + suffix + ".psx");
    guard.exes.push_back(prefixedCallbackExePath);
    auto prefixedCallbackBuffer = buildExeWithCodeBuiltCallbackTargetAfterPrefixLoads();
    std::ofstream prefixedCallbackFile(prefixedCallbackExePath, std::ios::binary);
    prefixedCallbackFile.write(reinterpret_cast<const char*>(prefixedCallbackBuffer.data()),
                               static_cast<std::streamsize>(prefixedCallbackBuffer.size()));
    prefixedCallbackFile.close();
    auto prefixedCallbackResult = pipeline.run(prefixedCallbackExePath.string());
    assert(prefixedCallbackResult.success);
    assert(!hasEmptyBoundaryWarning(prefixedCallbackResult.warnings));
    bool hasPrefixedCallbackTarget = false;
    for (const auto& function : prefixedCallbackResult.functions)
    {
        if (function.entryAddress == 0x80010040)
        {
            hasPrefixedCallbackTarget = true;
        }
    }
    assert(hasPrefixedCallbackTarget);

    std::filesystem::path jumpTableExePath =
        tempDir / ("psxrecomp_pipeline_jump_table_" + suffix + ".psx");
    guard.exes.push_back(jumpTableExePath);
    auto jumpTableBuffer = buildExeWithLocalJumpTableTargets();
    std::ofstream jumpTableFile(jumpTableExePath, std::ios::binary);
    jumpTableFile.write(reinterpret_cast<const char*>(jumpTableBuffer.data()),
                        static_cast<std::streamsize>(jumpTableBuffer.size()));
    jumpTableFile.close();
    auto jumpTableResult = pipeline.run(jumpTableExePath.string());
    assert(jumpTableResult.success);
    assert(!hasEmptyBoundaryWarning(jumpTableResult.warnings));
    bool hasJumpTableTarget0 = false;
    bool hasJumpTableTarget1 = false;
    for (const auto& function : jumpTableResult.functions)
    {
        if (function.entryAddress == 0x80010060)
        {
            hasJumpTableTarget0 = true;
        }
        if (function.entryAddress == 0x8001006C)
        {
            hasJumpTableTarget1 = true;
        }
    }
    assert(hasJumpTableTarget0);
    assert(hasJumpTableTarget1);

    std::filesystem::path mixedExePath = tempDir / ("psxrecomp_pipeline_mixed_" + suffix + ".psx");
    guard.exes.push_back(mixedExePath);
    auto mixedBuffer = buildExeWithCodeAndAsciiData();
    std::ofstream mixedFile(mixedExePath, std::ios::binary);
    mixedFile.write(reinterpret_cast<const char*>(mixedBuffer.data()),
                    static_cast<std::streamsize>(mixedBuffer.size()));
    mixedFile.close();
    auto mixedResult = pipeline.run(mixedExePath.string());
    assert(mixedResult.success);
    assert(!hasEmptyBoundaryWarning(mixedResult.warnings));
    const auto isUnsupportedFromAsciiData = [](const std::string& message)
    {
        if (message.find("Unsupported opcode:") == std::string::npos)
        {
            return false;
        }
        return message.find("0x80010008") != std::string::npos ||
               message.find("0x8001000c") != std::string::npos;
    };
    for (const auto& warning : mixedResult.warnings)
    {
        assert(!isUnsupportedFromAsciiData(warning));
    }
    for (const auto& diagnostic : mixedResult.diagnostics)
    {
        assert(!isUnsupportedFromAsciiData(diagnostic.message));
    }

    {
        std::ofstream ecmFile(ecmPath, std::ios::binary);
        const char marker[] = "not-an-exe";
        ecmFile.write(marker, static_cast<std::streamsize>(sizeof(marker)));
    }
    auto ecmResult = pipeline.run(ecmPath.string());
    assert(!ecmResult.success);
    assert(ecmResult.errorMessage.find("ECM-compressed") != std::string::npos);

    return 0;
}
