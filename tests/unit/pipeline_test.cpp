#include "psxrecomp/recompiler/pipeline.h"

#include "psxrecomp/iso/psx_exe_loader.h"

#include "pipeline_test_builders.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

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
