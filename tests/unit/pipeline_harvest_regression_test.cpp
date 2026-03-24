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

    // Test: pointer table whose entries point into a repeated 8-byte data-struct table
    // whose words start with MIPS-II trap instructions (TEQ, etc.).  These must NOT
    // become function seeds even when the table word is referenced by code via
    // LUI/ADDIU and has a nearby sibling pointer (i.e. scores >= 6 in the old heuristic).
    const std::filesystem::path trapDataExePath =
        tempDir / ("psxrecomp_pipeline_trap_data_" + suffix + ".psx");
    guard.exes.push_back(trapDataExePath);
    auto trapDataBuffer = buildExeWithPointerTableToTrapData();
    std::ofstream trapDataFile(trapDataExePath, std::ios::binary);
    trapDataFile.write(reinterpret_cast<const char*>(trapDataBuffer.data()),
                       static_cast<std::streamsize>(trapDataBuffer.size()));
    trapDataFile.close();

    auto trapDataResult = runPipeline(trapDataExePath, outputDir / "trap_data");
    // Pipeline must not abort (defensive fallback covers any residual speculative seed).
    assert(trapDataResult.success);
    // The TEQ-filled data addresses must never appear as function boundaries.
    assert(!hasFunctionEntry(trapDataResult, 0x80010030));
    assert(!hasFunctionEntry(trapDataResult, 0x80010038));
    // The real entry must still be present.
    assert(hasFunctionEntry(trapDataResult, 0x80010000));

    // Test: JAL delay-slot address must not become a function entry via harvesting.
    // The pointer table loaded by code references the delay-slot address twice;
    // the harvester would historically score it >= 6 and emit it as a pinned start,
    // slicing the calling function and dropping its delay slot from the IR window.
    const std::filesystem::path delaySlotSeedExePath =
        tempDir / ("psxrecomp_pipeline_delay_slot_seed_" + suffix + ".psx");
    guard.exes.push_back(delaySlotSeedExePath);
    auto delaySlotSeedBuffer = buildExeWithHarvestedDelaySlotSeed();
    std::ofstream delaySlotSeedFile(delaySlotSeedExePath, std::ios::binary);
    delaySlotSeedFile.write(reinterpret_cast<const char*>(delaySlotSeedBuffer.data()),
                            static_cast<std::streamsize>(delaySlotSeedBuffer.size()));
    delaySlotSeedFile.close();

    auto delaySlotSeedResult =
        runPipeline(delaySlotSeedExePath, outputDir / "delay_slot_seed");
    assert(delaySlotSeedResult.success);
    assert(!hasFunctionEntry(delaySlotSeedResult, 0x80010010));
    assert(hasFunctionEntry(delaySlotSeedResult, 0x80010000));
    assert(hasFunctionEntry(delaySlotSeedResult, 0x80010028));

    // Test: BGEZAL delay slot matching a prologue pattern must not cause the
    // gap-fill to create a boundary at the delay-slot address.
    const std::filesystem::path bgezalExePath =
        tempDir / ("psxrecomp_pipeline_bgezal_delay_slot_" + suffix + ".psx");
    guard.exes.push_back(bgezalExePath);
    auto bgezalBuffer = buildExeWithBgezalDelaySlotBoundary();
    std::ofstream bgezalFile(bgezalExePath, std::ios::binary);
    bgezalFile.write(reinterpret_cast<const char*>(bgezalBuffer.data()),
                     static_cast<std::streamsize>(bgezalBuffer.size()));
    bgezalFile.close();

    auto bgezalResult = runPipeline(bgezalExePath, outputDir / "bgezal_delay_slot");
    assert(bgezalResult.success);
    assert(hasFunctionEntry(bgezalResult, 0x80010000));
    assert(hasFunctionEntry(bgezalResult, 0x80010014));

    return 0;
}
