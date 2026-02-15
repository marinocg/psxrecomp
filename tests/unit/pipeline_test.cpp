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
        std::filesystem::path exe;
        std::filesystem::path ecm;
        std::filesystem::path out;
        ~CleanupGuard()
        {
            std::error_code error;
            std::filesystem::remove(exe, error);
            std::filesystem::remove(ecm, error);
            std::filesystem::remove_all(out, error);
        }
    };
    CleanupGuard guard{exePath, ecmPath, outputDir};

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

    std::filesystem::path mixedExePath = tempDir / ("psxrecomp_pipeline_mixed_" + suffix + ".psx");
    guard.exe = mixedExePath;
    auto mixedBuffer = buildExeWithCodeAndAsciiData();
    std::ofstream mixedFile(mixedExePath, std::ios::binary);
    mixedFile.write(reinterpret_cast<const char*>(mixedBuffer.data()),
                    static_cast<std::streamsize>(mixedBuffer.size()));
    mixedFile.close();
    auto mixedResult = pipeline.run(mixedExePath.string());
    assert(mixedResult.success);
    for (const auto& warning : mixedResult.warnings)
    {
        assert(warning.find("0x6c6c6548") == std::string::npos);
        assert(warning.find("0x6f77206f") == std::string::npos);
        assert(warning.find("0xdddddddd") == std::string::npos);
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
