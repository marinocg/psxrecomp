#include "psxrecomp/iso/iso_parser.h"
#include "psxrecomp/recompiler/pipeline.h"

#include "pipeline_candidate_test_helpers.h"
#include "pipeline_candidate_test_sections.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
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
    assert(std::filesystem::exists(std::filesystem::path(result.artifacts.resourcesPath) / "disc" /
                                   "data_track.bin"));
    assert(std::filesystem::exists(std::filesystem::path(result.artifacts.resourcesPath) / "disc" /
                                   "disc_layout.json"));
    assert(std::filesystem::exists(std::filesystem::path(result.artifacts.resourcesPath) / "disc" /
                                   "disc_hashes.json"));
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
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "disc/data_track.bin") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "disc/disc_layout.json") != result.artifacts.exportedResources.end());
    assert(std::find(result.artifacts.exportedResources.begin(),
                     result.artifacts.exportedResources.end(),
                     "disc/disc_hashes.json") != result.artifacts.exportedResources.end());
    {
        std::ifstream manifestFile(result.artifacts.resourceManifestPath);
        const std::string resourceManifest((std::istreambuf_iterator<char>(manifestFile)),
                                           std::istreambuf_iterator<char>());
        assert(resourceManifest.find("\"discTreePath\": \"index/disc_tree.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"discMetaPath\": \"index/disc_meta.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"discLayoutPath\": \"disc/disc_layout.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"discHashesPath\": \"disc/disc_hashes.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"recompInputsPath\": \"index/recomp_inputs.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"catalogPath\": \"index/catalog.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"filesystemEnabled\": true") != std::string::npos);
        assert(resourceManifest.find("\"discBlobEnabled\": true") != std::string::npos);
        assert(resourceManifest.find("\"discBlob\": {") != std::string::npos);
        assert(resourceManifest.find("\"hashPath\": \"disc/disc_hashes.json\"") !=
               std::string::npos);
        assert(resourceManifest.find("\"hashesPath\": ") == std::string::npos);
        assert(resourceManifest.find("\"discBlobSectorSize\": ") != std::string::npos);
        assert(resourceManifest.find("\"discBlobBytes\": ") != std::string::npos);
    }
    {
        const auto blobPath =
            std::filesystem::path(result.artifacts.resourcesPath) / "disc" / "data_track.bin";
        const auto layoutPath =
            std::filesystem::path(result.artifacts.resourcesPath) / "disc" / "disc_layout.json";
        const auto hashesPath =
            std::filesystem::path(result.artifacts.resourcesPath) / "disc" / "disc_hashes.json";

        std::ifstream layoutFile(layoutPath);
        const std::string layout((std::istreambuf_iterator<char>(layoutFile)),
                                 std::istreambuf_iterator<char>());
        assert(layout.find("\"format\": \"data_track_") != std::string::npos);

        std::ifstream hashesFile(hashesPath);
        const std::string hashes((std::istreambuf_iterator<char>(hashesFile)),
                                 std::istreambuf_iterator<char>());
        assert(hashes.find("\"sha1\": \"") != std::string::npos);

        const uint64_t sectorSize = findJsonIntegerField(layout, "sectorSize");
        const uint64_t lbaStart = findJsonIntegerField(layout, "lbaStart");
        const uint64_t lbaCount = findJsonIntegerField(layout, "lbaCount");
        const uint64_t blobSize = std::filesystem::file_size(blobPath);
        assert(sectorSize == 2048 || sectorSize == 2352);
        assert(lbaCount > 0);
        assert(blobSize > 0);
        assert(lbaCount * sectorSize == blobSize);

        std::ifstream blobFile(blobPath, std::ios::binary);
        const std::vector<uint8_t> blobData((std::istreambuf_iterator<char>(blobFile)),
                                            std::istreambuf_iterator<char>());
        assert(blobData.size() == blobSize);

        psxrecomp::iso::IsoParser lbaParser(isoPath.string());
        assert(lbaParser.open());
        assert(lbaParser.isValid());
        std::vector<uint8_t> sectorData;
        auto assertLbaMatches = [&](uint32_t lba)
        {
            assert(lba >= lbaStart);
            assert(static_cast<uint64_t>(lba - lbaStart) < lbaCount);
            const uint32_t relativeLba =
                static_cast<uint32_t>(static_cast<uint64_t>(lba) - lbaStart);
            if (sectorSize == 2352)
            {
                assert(lbaParser.canReadRaw2352());
                assert(lbaParser.readSectorRaw2352(relativeLba, sectorData));
            }
            else
            {
                assert(lbaParser.canReadUser2048());
                assert(lbaParser.readSectorUser2048(relativeLba, sectorData));
            }
            assert(sectorData.size() == sectorSize);
            const size_t blobOffset =
                static_cast<size_t>(lba - lbaStart) * static_cast<size_t>(sectorSize);
            assert(blobOffset + sectorData.size() <= blobData.size());
            assert(std::equal(sectorData.begin(), sectorData.end(), blobData.begin() + blobOffset));
        };

        const uint32_t startLba = static_cast<uint32_t>(lbaStart);
        const uint32_t endLbaExclusive = static_cast<uint32_t>(lbaStart + lbaCount);
        assertLbaMatches(startLba);
        if (lbaCount > 1)
        {
            assertLbaMatches(startLba + 1);
        }
        assertLbaMatches(startLba + static_cast<uint32_t>(lbaCount / 2));
        if (lbaCount > 1)
        {
            assertLbaMatches(endLbaExclusive - 2);
        }
        assertLbaMatches(endLbaExclusive - 1);
        uint32_t seed = 0x12345678U;
        for (int index = 0; index < 20; ++index)
        {
            seed = seed * 1664525U + 1013904223U;
            const uint32_t randomLba = startLba + (seed % static_cast<uint32_t>(lbaCount));
            assertLbaMatches(randomLba);
        }
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
    {
        auto rerunOptions = options;
        rerunOptions.outputDirectory = (outputDir / "from_resources_workspace").string();
        psxrecomp::recompiler::RecompilationPipeline workspacePipeline(rerunOptions);
        const auto workspaceResult = workspacePipeline.run(result.artifacts.resourcesPath);
        assert(workspaceResult.success);
        assert(workspaceResult.selectionInfo.selectedPath == result.selectionInfo.selectedPath);

        const auto findCandidateByPath =
            [](const psxrecomp::recompiler::PipelineResult& runResult,
               const std::string& path) -> const psxrecomp::recompiler::ExeCandidateInfo*
        {
            for (const auto& candidate : runResult.exeCandidates)
            {
                if (candidate.path == path)
                {
                    return &candidate;
                }
            }
            return nullptr;
        };
        const auto* baselineCandidate =
            findCandidateByPath(result, result.selectionInfo.selectedPath);
        const auto* workspaceCandidate =
            findCandidateByPath(workspaceResult, workspaceResult.selectionInfo.selectedPath);
        assert(baselineCandidate != nullptr);
        assert(workspaceCandidate != nullptr);
        assert(workspaceCandidate->hash == baselineCandidate->hash);
        assert(workspaceCandidate->loadAddress == baselineCandidate->loadAddress);
        assert(workspaceCandidate->loadSize == baselineCandidate->loadSize);
        assert(workspaceCandidate->entryPoint == baselineCandidate->entryPoint);

        const auto buildFunctionSignature =
            [](const psxrecomp::recompiler::PipelineResult& runResult)
        {
            std::ostringstream stream;
            for (const auto& function : runResult.functions)
            {
                stream << function.entryAddress << ":" << function.endAddress << ":"
                       << function.directCalls.size() << ":" << function.indirectCallCount << ";";
            }
            return stream.str();
        };
        assert(buildFunctionSignature(workspaceResult) == buildFunctionSignature(result));
        assert(
            std::filesystem::exists(std::filesystem::path(workspaceResult.artifacts.resourcesPath) /
                                    "index" / "disc_tree.json"));
        assert(
            std::filesystem::exists(std::filesystem::path(workspaceResult.artifacts.resourcesPath) /
                                    "index" / "disc_meta.json"));
        assert(std::find(workspaceResult.artifacts.exportedResources.begin(),
                         workspaceResult.artifacts.exportedResources.end(),
                         "index/disc_tree.json") !=
               workspaceResult.artifacts.exportedResources.end());
        assert(std::find(workspaceResult.artifacts.exportedResources.begin(),
                         workspaceResult.artifacts.exportedResources.end(),
                         "index/disc_meta.json") !=
               workspaceResult.artifacts.exportedResources.end());
    }
    {
        const std::filesystem::path maliciousWorkspace =
            outputDir / "malicious_workspace_path_traversal";
        std::error_code copyError;
        std::filesystem::remove_all(maliciousWorkspace, copyError);
        copyError.clear();
        std::filesystem::copy(result.artifacts.resourcesPath, maliciousWorkspace,
                              std::filesystem::copy_options::recursive, copyError);
        assert(!copyError);

        {
            std::ofstream recompInputsFile(maliciousWorkspace / "index" / "recomp_inputs.json",
                                           std::ios::binary | std::ios::trunc);
            recompInputsFile << "{\n"
                                "  \"schemaVersion\": \"1.0\",\n"
                                "  \"boot\": {\n"
                                "    \"isoPath\": \"GAMEB.EXE\",\n"
                                "    \"exportedPath\": \"fs/GAMEB.EXE\"\n"
                                "  },\n"
                                "  \"executables\": [\n"
                                "    {\n"
                                "      \"isoPath\": \"GAMEB.EXE\",\n"
                                "      \"exportedPath\": \"../outside/GAMEB.EXE\"\n"
                                "    }\n"
                                "  ]\n"
                                "}\n";
        }

        auto maliciousOptions = options;
        maliciousOptions.outputDirectory = (outputDir / "malicious_workspace_output").string();
        psxrecomp::recompiler::RecompilationPipeline maliciousPipeline(maliciousOptions);
        const auto maliciousResult = maliciousPipeline.run(maliciousWorkspace.string());
        assert(!maliciousResult.success);
        assert(maliciousResult.errorMessage.find("Failed to parse resources workspace:") !=
               std::string::npos);
    }
    {
        auto overlapOptions = options;
        overlapOptions.outputDirectory =
            (std::filesystem::path(result.artifacts.resourcesPath) / "overlap_out").string();
        psxrecomp::recompiler::RecompilationPipeline overlapPipeline(overlapOptions);
        const auto overlapResult = overlapPipeline.run(result.artifacts.resourcesPath);
        assert(!overlapResult.success);
        assert(overlapResult.errorMessage.find("overlap") != std::string::npos);
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

    auto noBlobOptions = makeOptions();
    noBlobOptions.outputDirectory = (outputDir / "no_disc_blob").string();
    noBlobOptions.resourceExport.discBlob.enabled = false;
    psxrecomp::recompiler::RecompilationPipeline noBlobPipeline(noBlobOptions);
    const auto noBlobResult = noBlobPipeline.run(policyIsoPath.string());
    assert(noBlobResult.success);
    assert(!std::filesystem::exists(std::filesystem::path(noBlobResult.artifacts.resourcesPath) /
                                    "disc" / "data_track.bin"));
    assert(!std::filesystem::exists(std::filesystem::path(noBlobResult.artifacts.resourcesPath) /
                                    "disc" / "disc_layout.json"));
    assert(!std::filesystem::exists(std::filesystem::path(noBlobResult.artifacts.resourcesPath) /
                                    "disc" / "disc_hashes.json"));
    assert(std::find(noBlobResult.artifacts.exportedResources.begin(),
                     noBlobResult.artifacts.exportedResources.end(),
                     "disc/data_track.bin") == noBlobResult.artifacts.exportedResources.end());
    assert(std::find(noBlobResult.artifacts.exportedResources.begin(),
                     noBlobResult.artifacts.exportedResources.end(),
                     "disc/disc_layout.json") == noBlobResult.artifacts.exportedResources.end());
    assert(std::find(noBlobResult.artifacts.exportedResources.begin(),
                     noBlobResult.artifacts.exportedResources.end(),
                     "disc/disc_hashes.json") == noBlobResult.artifacts.exportedResources.end());
    {
        std::ifstream manifestFile(noBlobResult.artifacts.resourceManifestPath);
        const std::string manifest((std::istreambuf_iterator<char>(manifestFile)),
                                   std::istreambuf_iterator<char>());
        assert(manifest.find("\"discLayoutPath\": null") != std::string::npos);
        assert(manifest.find("\"discHashesPath\": null") != std::string::npos);
        assert(manifest.find("\"discBlobEnabled\": false") != std::string::npos);
        assert(manifest.find("\"blobPath\": \"\"") != std::string::npos);
        assert(manifest.find("\"layoutPath\": \"\"") != std::string::npos);
        assert(manifest.find("\"hashPath\": \"\"") != std::string::npos);
        assert(manifest.find("\"hashesPath\": ") == std::string::npos);
        assert(findJsonIntegerField(manifest, "discBlobLbaCount") == 0);
        assert(findJsonIntegerField(manifest, "discBlobBytes") == 0);
    }

    std::filesystem::remove(policyIsoPath);

    auto offsetCueImage = createCueWithDataTrackOffset("DISC_OFFSET", 150);
    auto offsetOptions = makeOptions();
    offsetOptions.outputDirectory = (outputDir / "disc_offset_layout").string();
    psxrecomp::recompiler::RecompilationPipeline offsetPipeline(offsetOptions);
    const auto offsetResult = offsetPipeline.run(offsetCueImage.cuePath.string());
    assert(offsetResult.success);
    {
        const auto layoutPath = std::filesystem::path(offsetResult.artifacts.resourcesPath) /
                                "disc" / "disc_layout.json";
        std::ifstream layoutFile(layoutPath);
        const std::string layout((std::istreambuf_iterator<char>(layoutFile)),
                                 std::istreambuf_iterator<char>());
        const uint64_t lbaStart = findJsonIntegerField(layout, "lbaStart");
        assert(lbaStart == 150);
        const uint64_t lbaCount = findJsonIntegerField(layout, "lbaCount");
        assert(lbaCount == 64);

        auto findFirstExtentLbaForPath = [&](const std::string& path)
        {
            const std::string pathMarker = "\"path\": \"" + path + "\"";
            const size_t pathPos = layout.find(pathMarker);
            assert(pathPos != std::string::npos);
            const std::string lbaMarker = "\"lba\": ";
            const size_t lbaPos = layout.find(lbaMarker, pathPos);
            assert(lbaPos != std::string::npos);
            size_t valueStart = lbaPos + lbaMarker.size();
            size_t valueEnd = valueStart;
            while (valueEnd < layout.size() &&
                   std::isdigit(static_cast<unsigned char>(layout[valueEnd])) != 0)
            {
                ++valueEnd;
            }
            assert(valueEnd > valueStart);
            return static_cast<uint64_t>(std::strtoull(
                layout.substr(valueStart, valueEnd - valueStart).c_str(), nullptr, 10));
        };

        assert(findFirstExtentLbaForPath("SYSTEM.CNF") == 171);
        assert(findFirstExtentLbaForPath("GAME.EXE") == 172);
    }
    std::filesystem::remove(offsetCueImage.cuePath);
    std::filesystem::remove(offsetCueImage.binPath);

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

    runEmbeddedScanDeterminismScenario(options, outputDir);

    std::error_code error;
    std::filesystem::remove_all(outputDir, error);
    return 0;
}
