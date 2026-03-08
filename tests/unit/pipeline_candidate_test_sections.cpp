#include "pipeline_candidate_test_sections.h"

#include "pipeline_candidate_test_helpers.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace pipeline_candidate_test_support
{

void runEmbeddedScanDeterminismScenario(const psxrecomp::recompiler::PipelineOptions& baseOptions,
                                        const std::filesystem::path& outputDir)
{
    auto makeOptions = [&]()
    {
        psxrecomp::recompiler::PipelineOptions modeOptions = baseOptions;
        modeOptions.outputDirectory = outputDir.string();
        modeOptions.enableOptimizations = false;
        modeOptions.preserveSymbols = false;
        modeOptions.verbose = false;
        modeOptions.manifestTimestamp = "2024-01-01T00:00:00Z";
        return modeOptions;
    };

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
        [[maybe_unused]] const size_t embeddedScanPos = manifest.find("\"embeddedScan\"");
        assert(embeddedScanPos != std::string::npos);
        assert(manifest.find("\"enabled\": true", embeddedScanPos) != std::string::npos);
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
}

} // namespace pipeline_candidate_test_support
