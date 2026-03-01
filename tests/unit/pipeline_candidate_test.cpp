#include "psxrecomp/recompiler/pipeline.h"

#include "pipeline_candidate_test_helpers.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace pipeline_candidate_test_support;

int main()
{
    auto isoPath = createIsoWithExecutables("DISC_BOOT", "BOOT = cdrom:\\GAMEB.EXE;1\n");
    auto tempDir = std::filesystem::temp_directory_path();
    auto outputDir = tempDir / "psxrecomp_candidates_out";

    psxrecomp::recompiler::PipelineOptions options;
    options.outputDirectory = outputDir.string();
    options.enableOptimizations = false;
    options.preserveSymbols = false;
    options.verbose = false;
    options.manifestTimestamp = "2024-01-01T00:00:00Z";

    psxrecomp::recompiler::RecompilationPipeline pipeline(options);
    auto result = pipeline.run(isoPath.string());
    assert(result.success);
    assert(result.selectionInfo.selectedPath == "GAMEB.EXE");
    assert(!result.artifacts.resourceManifestPath.empty());
    assert(std::filesystem::exists(result.artifacts.resourceManifestPath));
    assert(std::filesystem::exists(std::filesystem::path(result.artifacts.resourcesPath) / "index" /
                                   "disc_tree.json"));
    assert(std::filesystem::exists(std::filesystem::path(result.artifacts.resourcesPath) / "index" /
                                   "disc_meta.json"));
    assert(std::filesystem::exists(std::filesystem::path(result.artifacts.resourcesPath) / "index" /
                                   "recomp_inputs.json"));
    assert(std::filesystem::exists(std::filesystem::path(result.artifacts.resourcesPath) / "index" /
                                   "catalog.json"));
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/resources_manifest.json") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/disc_tree.json") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/disc_meta.json") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/recomp_inputs.json") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "index/catalog.json") != result.artifacts.exportedResources.end());
    {
        std::ifstream manifestFile(result.artifacts.resourceManifestPath);
        const std::string resourceManifest((std::istreambuf_iterator<char>(manifestFile)),
                                           std::istreambuf_iterator<char>());
        assert(resourceManifest.find("\"discTreePath\": \"index/disc_tree.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"discMetaPath\": \"index/disc_meta.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"recompInputsPath\": \"index/recomp_inputs.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"catalogPath\": \"index/catalog.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"filesystemEnabled\": true") != std::string::npos);
    }
    {
        std::ifstream recompInputsFile(std::filesystem::path(result.artifacts.resourcesPath) /
                                       "index" / "recomp_inputs.json");
        const std::string recompInputs((std::istreambuf_iterator<char>(recompInputsFile)),
                                       std::istreambuf_iterator<char>());
        assert(recompInputs.find("\"isoPath\": \"GAMEB.EXE\"") != std::string::npos);
        assert(recompInputs.find("\"exportedPath\": \"fs/GAMEB.EXE\"") != std::string::npos);
        assert(recompInputs.find("\"isoPath\": \"GAMEA.EXE\"") != std::string::npos);
        assert(recompInputs.find("\"exportedPath\": \"fs/GAMEA.EXE\"") != std::string::npos);
        assert(recompInputs.find("\"loadAddr\": \"0x80010000\"") != std::string::npos);
        assert(recompInputs.find("\"loadAddr\": \"0x80020000\"") != std::string::npos);
    }
    {
        std::ifstream catalogFile(std::filesystem::path(result.artifacts.resourcesPath) / "index" /
                                  "catalog.json");
        const std::string catalog((std::istreambuf_iterator<char>(catalogFile)),
                                  std::istreambuf_iterator<char>());
        assert(catalog.find("\"isoPath\": \"GAMEA.EXE\"") != std::string::npos);
        assert(catalog.find("\"isoPath\": \"GAMEB.EXE\"") != std::string::npos);
        assert(catalog.find("\"sha1\": \"") != std::string::npos);
        assert(
            catalogEntryHasSha1(catalog, "GAMEA.EXE", "c81ef78add2542090c4124b515612c2b5b42b5a2"));
        assert(
            catalogEntryHasSha1(catalog, "GAMEB.EXE", "071bd5165a34451b85d3c0835f990394eec29a44"));
        assert(catalogEntryHasType(catalog, "GAMEA.EXE", "psx_exe"));
        assert(catalogEntryHasType(catalog, "GAMEB.EXE", "psx_exe"));
    }
    {
        std::ifstream pipelineManifest(result.artifacts.manifestPath);
        const std::string pipelineManifestText((std::istreambuf_iterator<char>(pipelineManifest)),
                                               std::istreambuf_iterator<char>());
        assert(pipelineManifestText.find("\"resourceRoot\"") != std::string::npos);
        assert(pipelineManifestText.find("\"resourceManifest\"") != std::string::npos);
    }

    std::filesystem::remove(isoPath);

    auto isoPathSorted = createIsoWithExecutables("DISC_SORT", "");
    auto resultSorted = pipeline.run(isoPathSorted.string());
    assert(resultSorted.success);
    assert(resultSorted.selectionInfo.selectedPath == "GAMEA.EXE");
    std::filesystem::remove(isoPathSorted);

    auto policyIsoPath = createIsoForExportPolicy("DISC_POLICY");
    auto makeOptions = [&]()
    {
        psxrecomp::recompiler::PipelineOptions modeOptions = options;
        modeOptions.outputDirectory = outputDir.string();
        modeOptions.enableOptimizations = false;
        modeOptions.preserveSymbols = false;
        modeOptions.verbose = false;
        modeOptions.manifestTimestamp = "2024-01-01T00:00:00Z";
        return modeOptions;
    };
    auto countFsExports = [](const psxrecomp::recompiler::PipelineResult& runResult)
    {
        return std::count_if(runResult.artifacts.exportedResources.begin(),
                             runResult.artifacts.exportedResources.end(),
                             [](const std::string& path) { return path.rfind("fs/", 0) == 0; });
    };
    auto hasExportedPath =
        [](const psxrecomp::recompiler::PipelineResult& runResult, const std::string& path)
    {
        return std::find(runResult.artifacts.exportedResources.begin(),
                         runResult.artifacts.exportedResources.end(),
                         path) != runResult.artifacts.exportedResources.end();
    };

    auto minimalOptions = makeOptions();
    minimalOptions.resourceExport.fsMode =
        psxrecomp::recompiler::PipelineOptions::ResourceExportOptions::FsMode::Minimal;
    psxrecomp::recompiler::RecompilationPipeline minimalPipeline(minimalOptions);
    const auto minimalResult = minimalPipeline.run(policyIsoPath.string());
    assert(minimalResult.success);

    auto smartOptions = makeOptions();
    smartOptions.resourceExport.fsMode =
        psxrecomp::recompiler::PipelineOptions::ResourceExportOptions::FsMode::Smart;
    smartOptions.resourceExport.maxSingleFileBytes = 2048;
    smartOptions.resourceExport.maxTotalBytes = 16384;
    psxrecomp::recompiler::RecompilationPipeline smartPipeline(smartOptions);
    const auto smartResult = smartPipeline.run(policyIsoPath.string());
    assert(smartResult.success);

    auto fullOptions = makeOptions();
    fullOptions.resourceExport.fsMode =
        psxrecomp::recompiler::PipelineOptions::ResourceExportOptions::FsMode::Full;
    psxrecomp::recompiler::RecompilationPipeline fullPipeline(fullOptions);
    const auto fullResult = fullPipeline.run(policyIsoPath.string());
    assert(fullResult.success);

    const size_t minimalFsCount = countFsExports(minimalResult);
    const size_t smartFsCount = countFsExports(smartResult);
    const size_t fullFsCount = countFsExports(fullResult);
    assert(minimalFsCount == 3);
    assert(minimalFsCount < smartFsCount);
    assert(smartFsCount < fullFsCount);

    assert(hasExportedPath(minimalResult, "fs/SYSTEM.CNF"));
    assert(hasExportedPath(minimalResult, "fs/GAMEB.EXE"));
    assert(hasExportedPath(minimalResult, "fs/GAMEA.EXE"));
    assert(hasExportedPath(smartResult, "fs/SYSTEM.CNF"));
    assert(hasExportedPath(smartResult, "fs/GAMEB.EXE"));
    assert(hasExportedPath(fullResult, "fs/SYSTEM.CNF"));
    assert(hasExportedPath(fullResult, "fs/GAMEB.EXE"));

    std::filesystem::remove(policyIsoPath);

    auto timIsoPath = createIsoWithTimBin("DISC_TIM_BIN");
    auto timResult = pipeline.run(timIsoPath.string());
    assert(timResult.success);
    {
        std::ifstream catalogFile(std::filesystem::path(timResult.artifacts.resourcesPath) /
                                  "index" / "catalog.json");
        const std::string catalog((std::istreambuf_iterator<char>(catalogFile)),
                                  std::istreambuf_iterator<char>());
        assert(catalog.find("\"isoPath\": \"TEXTURE.BIN\"") != std::string::npos);
        assert(catalogEntryHasType(catalog, "TEXTURE.BIN", "tim"));
    }
    std::filesystem::remove(timIsoPath);

    auto embeddedIsoPath = createIsoWithEmbeddedTimContainer("DISC_EMBEDDED");
    auto embeddedOptions = makeOptions();
    embeddedOptions.outputDirectory = (outputDir / "embedded_scan_run1").string();
    embeddedOptions.resourceExport.fsMode =
        psxrecomp::recompiler::PipelineOptions::ResourceExportOptions::FsMode::Minimal;
    embeddedOptions.resourceExport.enableEmbeddedScan = true;
    psxrecomp::recompiler::RecompilationPipeline embeddedPipeline(embeddedOptions);
    const auto embeddedResult = embeddedPipeline.run(embeddedIsoPath.string());
    assert(embeddedResult.success);
    std::string embeddedPathRun1;
    {
        std::ifstream manifestFile(embeddedResult.artifacts.resourceManifestPath);
        const std::string manifest((std::istreambuf_iterator<char>(manifestFile)),
                                   std::istreambuf_iterator<char>());
        assert(manifest.find("\"embeddedScan\": {\n      \"enabled\": true") != std::string::npos);
        assert(findJsonIntegerField(manifest, "containersScanned") >= 1);
        assert(findJsonIntegerField(manifest, "hitsExtracted") >= 1);
    }
    {
        std::ifstream catalogFile(std::filesystem::path(embeddedResult.artifacts.resourcesPath) /
                                  "index" / "catalog.json");
        const std::string catalog((std::istreambuf_iterator<char>(catalogFile)),
                                  std::istreambuf_iterator<char>());
        assert(catalog.find("\"sourceKind\": \"embedded\"") != std::string::npos);
        assert(catalog.find("\"containerIsoPath\": \"CONTAINER.BIN\"") != std::string::npos);
        assert(catalog.find("\"offset\": 4660") != std::string::npos);
        assert(catalog.find("\"sha1\": \"8ba483b23059badb870684268fb7f7bc7153bcce\"") !=
               std::string::npos);
        embeddedPathRun1 = findEmbeddedExportedPath(catalog, "CONTAINER.BIN");
        assert(!embeddedPathRun1.empty());
    }
    assert(std::filesystem::exists(std::filesystem::path(embeddedResult.artifacts.resourcesPath) /
                                   embeddedPathRun1));

    auto embeddedOptions2 = embeddedOptions;
    embeddedOptions2.outputDirectory = (outputDir / "embedded_scan_run2").string();
    psxrecomp::recompiler::RecompilationPipeline embeddedPipeline2(embeddedOptions2);
    const auto embeddedResult2 = embeddedPipeline2.run(embeddedIsoPath.string());
    assert(embeddedResult2.success);
    {
        std::ifstream catalogFile(std::filesystem::path(embeddedResult2.artifacts.resourcesPath) /
                                  "index" / "catalog.json");
        const std::string catalog((std::istreambuf_iterator<char>(catalogFile)),
                                  std::istreambuf_iterator<char>());
        const std::string embeddedPathRun2 = findEmbeddedExportedPath(catalog, "CONTAINER.BIN");
        assert(!embeddedPathRun2.empty());
        assert(embeddedPathRun2 == embeddedPathRun1);
    }
    std::filesystem::remove(embeddedIsoPath);

    std::error_code error;
    std::filesystem::remove_all(outputDir, error);
    return 0;
}
