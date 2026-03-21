#include "psxrecomp/recompiler/pipeline.h"

#include "pipeline_test_builders.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace
{

struct CleanupGuard
{
    std::vector<std::filesystem::path> exes;
    std::filesystem::path out;

    ~CleanupGuard()
    {
        std::error_code error;
        for (const auto& exe : exes)
        {
            std::filesystem::remove(exe, error);
        }
        std::filesystem::remove_all(out, error);
    }
};

psxrecomp::recompiler::PipelineResult runPipeline(const std::filesystem::path& exePath,
                                                  const std::filesystem::path& outputDir)
{
    psxrecomp::recompiler::PipelineOptions options;
    options.outputDirectory = outputDir.string();
    options.enableOptimizations = false;
    options.preserveSymbols = false;
    options.verbose = false;
    options.manifestTimestamp = "2024-01-01T00:00:00Z";

    psxrecomp::recompiler::RecompilationPipeline pipeline(options);
    return pipeline.run(exePath.string());
}

bool hasFunctionEntry(const psxrecomp::recompiler::PipelineResult& result, psxrecomp::u32 address)
{
    for (const auto& function : result.functions)
    {
        if (function.entryAddress == address)
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    auto tempDir = std::filesystem::temp_directory_path();
    auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device randomDevice;
    std::uniform_int_distribution<int> dist(0, 0xFFFF);
    const std::string suffix = std::to_string(timestamp) + "_" + std::to_string(dist(randomDevice));

    const std::filesystem::path outputDir = tempDir / ("psxrecomp_pipeline_harvest_" + suffix);
    CleanupGuard guard{{}, outputDir};

    const std::filesystem::path delaySlotExePath =
        tempDir / ("psxrecomp_pipeline_delay_slot_store_" + suffix + ".psx");
    guard.exes.push_back(delaySlotExePath);
    auto delaySlotBuffer = buildExeWithDelaySlotStoredDispatchTarget();
    std::ofstream delaySlotFile(delaySlotExePath, std::ios::binary);
    delaySlotFile.write(reinterpret_cast<const char*>(delaySlotBuffer.data()),
                        static_cast<std::streamsize>(delaySlotBuffer.size()));
    delaySlotFile.close();

    auto delaySlotResult = runPipeline(delaySlotExePath, outputDir / "delay_slot");
    assert(delaySlotResult.success);
    assert(hasFunctionEntry(delaySlotResult, 0x80010040));

    const std::filesystem::path returnedPointerExePath =
        tempDir / ("psxrecomp_pipeline_returned_pointer_" + suffix + ".psx");
    guard.exes.push_back(returnedPointerExePath);
    auto returnedPointerBuffer = buildExeWithReturnedDispatchTargetStore();
    std::ofstream returnedPointerFile(returnedPointerExePath, std::ios::binary);
    returnedPointerFile.write(reinterpret_cast<const char*>(returnedPointerBuffer.data()),
                              static_cast<std::streamsize>(returnedPointerBuffer.size()));
    returnedPointerFile.close();

    auto returnedPointerResult =
        runPipeline(returnedPointerExePath, outputDir / "returned_pointer");
    assert(returnedPointerResult.success);
    assert(hasFunctionEntry(returnedPointerResult, 0x80010040));

    return 0;
}
