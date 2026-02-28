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

    std::filesystem::path mixedExePath = tempDir / ("psxrecomp_pipeline_mixed_" + suffix + ".psx");
    guard.exes.push_back(mixedExePath);
    auto mixedBuffer = buildExeWithCodeAndAsciiData();
    std::ofstream mixedFile(mixedExePath, std::ios::binary);
    mixedFile.write(reinterpret_cast<const char*>(mixedBuffer.data()),
                    static_cast<std::streamsize>(mixedBuffer.size()));
    mixedFile.close();
    auto mixedResult = pipeline.run(mixedExePath.string());
    assert(mixedResult.success);
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
