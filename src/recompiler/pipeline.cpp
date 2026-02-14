#include "psxrecomp/recompiler/pipeline.h"

#include "pipeline_helpers.h"
#include "psxrecomp/disasm/analysis.h"
#include "psxrecomp/disasm/instruction.h"
#include "psxrecomp/ir/control_flow.h"
#include "psxrecomp/ir/mips_ir_builder.h"
#include "psxrecomp/ir/optimizations.h"
#include "psxrecomp/iso/iso_boot.h"
#include "psxrecomp/iso/iso_parser.h"
#include "psxrecomp/iso/psx_exe_loader.h"
#include "psxrecomp/recompiler/codegen.h"

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <optional>
#include <sstream>

namespace psxrecomp
{
namespace recompiler
{

namespace
{
PipelineResult buildPipelineError(const std::string& message,
                                  const std::vector<std::string>& warnings,
                                  const std::vector<PipelineDiagnostic>& diagnostics,
                                  const std::vector<ExeCandidateInfo>& candidates)
{
    PipelineResult result;
    result.success = false;
    result.errorMessage = message;
    result.warnings = warnings;
    result.diagnostics = diagnostics;
    result.exeCandidates = candidates;
    return result;
}
} // namespace

RecompilationPipeline::RecompilationPipeline(PipelineOptions options)
    : m_options(std::move(options))
{
}

PipelineResult RecompilationPipeline::run(const std::string& inputPath)
{
    if (inputPath.empty())
    {
        return buildPipelineError("Input path is empty.", {}, {}, {});
    }

    if (m_options.outputDirectory.empty())
    {
        return buildPipelineError("Output directory is not set.", {}, {}, {});
    }

    const std::vector<std::string> discPaths =
        m_options.discPaths.empty() ? std::vector<std::string>{inputPath} : m_options.discPaths;
    const size_t activeDiscIndex =
        discPaths.empty() ? 0U : std::min(m_options.activeDiscIndex, discPaths.size() - 1U);
    const std::string activeDiscPath = discPaths.empty() ? inputPath : discPaths[activeDiscIndex];
    const std::filesystem::path inputFsPath(activeDiscPath);

    iso::PsxExeImage exeImage{};
    std::vector<std::string> warnings;
    std::vector<PipelineDiagnostic> diagnostics;
    PipelineResult result;
    auto appendIsoParserErrors = [&](const iso::IsoParser& parser, const std::string& context)
    {
        for (const auto& error : parser.getErrors())
        {
            PipelineDiagnostic entry;
            entry.code = "IsoParserError";
            entry.severity = "error";
            entry.message = context + ": " + error;
            entry.context.file = activeDiscPath;
            diagnostics.push_back(entry);
        }
    };
    result.discSet = detail::buildDiscSetMetadata(discPaths, activeDiscIndex, warnings);

    const std::string selectionRule =
        "Prefer SYSTEM.CNF BOOT entry when valid; otherwise select sorted candidates by path, "
        "load address, size, entry point, hash, validity, then path.";
    result.selectionInfo.rule = selectionRule;

    auto fail = [&](const std::string& message)
    {
        PipelineResult failed =
            buildPipelineError(message, warnings, diagnostics, result.exeCandidates);
        failed.selectionInfo = result.selectionInfo;
        failed.discSet = result.discSet;
        return failed;
    };

    if (detail::isIsoLikePath(inputFsPath))
    {
        iso::IsoParser parser(activeDiscPath);
        if (!parser.open() || !parser.isValid())
        {
            appendIsoParserErrors(parser, "Failed to parse ISO image");
            std::ostringstream error;
            error << "Failed to open ISO image.";
            if (!parser.getLastError().empty())
            {
                error << " Last parser error: " << parser.getLastError();
            }
            return fail(error.str());
        }

        std::vector<std::string> candidatePaths = parser.listExecutables();
        auto systemCnf = parser.extractFile("SYSTEM.CNF");
        const std::string bootPath = iso::detail::parseBootPathFromSystemCnf(systemCnf);
        if (!bootPath.empty())
        {
            candidatePaths.push_back(bootPath);
        }
        std::vector<std::string> normalized;
        std::vector<std::string> uniquePaths;
        for (const auto& path : candidatePaths)
        {
            std::string key = detail::toLower(path);
            if (std::find(normalized.begin(), normalized.end(), key) == normalized.end())
            {
                normalized.push_back(key);
                uniquePaths.push_back(path);
            }
        }

        if (uniquePaths.empty())
        {
            appendIsoParserErrors(parser, "Executable discovery failed");
            return fail("No PSX executable found in ISO image.");
        }

        for (const auto& path : uniquePaths)
        {
            ExeCandidateInfo candidate;
            candidate.path = path;
            std::vector<u8> exeData = parser.extractFile(path);
            if (exeData.empty())
            {
                PipelineDiagnostic entry;
                entry.code = "ExeCandidateExtractFailed";
                entry.severity = "error";
                entry.message = "Failed to extract executable data.";
                entry.context.file = path;
                diagnostics.push_back(entry);
                candidate.diagnostics.push_back(std::move(entry));
                result.exeCandidates.push_back(std::move(candidate));
                continue;
            }

            iso::PsxExeDiagnostics exeDiagnostics;
            iso::PsxExeImage image{};
            if (!iso::PsxExeLoader::loadImage(exeData, image, &exeDiagnostics))
            {
                detail::appendDiagnostics(diagnostics, warnings, exeDiagnostics, path);
                for (const auto& entry : exeDiagnostics.entries)
                {
                    candidate.diagnostics.push_back(detail::toPipelineDiagnostic(entry, path));
                }
                result.exeCandidates.push_back(std::move(candidate));
                continue;
            }

            candidate.valid = true;
            candidate.loadAddress = image.header.loadAddress;
            candidate.loadSize = image.header.loadSize;
            candidate.entryPoint = image.entryPoint.pc;
            candidate.hash = detail::formatHex(detail::fnv1a64(image.programData), 16);
            detail::appendDiagnostics(diagnostics, warnings, exeDiagnostics, path);
            for (const auto& entry : exeDiagnostics.entries)
            {
                candidate.diagnostics.push_back(detail::toPipelineDiagnostic(entry, path));
            }
            result.exeCandidates.push_back(candidate);
        }

        auto candidateLess = [&](const ExeCandidateInfo& lhs, const ExeCandidateInfo& rhs)
        {
            const std::string lhsKey = detail::toLower(lhs.path);
            const std::string rhsKey = detail::toLower(rhs.path);
            if (lhsKey != rhsKey)
            {
                return lhsKey < rhsKey;
            }
            if (lhs.loadAddress != rhs.loadAddress)
            {
                return lhs.loadAddress < rhs.loadAddress;
            }
            if (lhs.loadSize != rhs.loadSize)
            {
                return lhs.loadSize < rhs.loadSize;
            }
            if (lhs.entryPoint != rhs.entryPoint)
            {
                return lhs.entryPoint < rhs.entryPoint;
            }
            if (lhs.hash != rhs.hash)
            {
                return lhs.hash < rhs.hash;
            }
            if (lhs.valid != rhs.valid)
            {
                return lhs.valid;
            }
            return lhs.path < rhs.path;
        };
        std::sort(result.exeCandidates.begin(), result.exeCandidates.end(), candidateLess);

        std::optional<size_t> selectedIndex;
        if (!bootPath.empty())
        {
            for (size_t index = 0; index < result.exeCandidates.size(); ++index)
            {
                const auto& candidate = result.exeCandidates[index];
                if (candidate.valid && detail::toLower(candidate.path) == detail::toLower(bootPath))
                {
                    selectedIndex = index;
                    result.selectionInfo.reason = "Selected SYSTEM.CNF BOOT candidate.";
                    break;
                }
            }
        }
        if (!selectedIndex.has_value())
        {
            for (size_t index = 0; index < result.exeCandidates.size(); ++index)
            {
                if (result.exeCandidates[index].valid)
                {
                    selectedIndex = index;
                    result.selectionInfo.reason = "Selected first valid candidate after sorting.";
                    break;
                }
            }
        }
        if (!selectedIndex.has_value())
        {
            return fail("No valid PSX executable candidate found.");
        }

        const auto& selected = result.exeCandidates[selectedIndex.value()];
        result.selectionInfo.selectedPath = selected.path;
        std::vector<u8> exeData = parser.extractFile(selected.path);
        if (!iso::PsxExeLoader::loadImage(exeData, exeImage, nullptr))
        {
            appendIsoParserErrors(parser, "Selected executable extraction failed");
            return fail("Failed to parse selected PSX executable.");
        }
    }
    else
    {
        iso::PsxExeDiagnostics exeDiagnostics;
        if (!iso::PsxExeLoader::loadFromFile(activeDiscPath, exeImage, &exeDiagnostics))
        {
            detail::appendDiagnostics(diagnostics, warnings, exeDiagnostics, activeDiscPath);
            std::ostringstream errorStream;
            errorStream << "Failed to load PSX executable.";
            if (detail::toLower(inputFsPath.extension().string()) == ".ecm")
            {
                errorStream << " Input appears to be ECM-compressed. Decode the image/executable "
                               "to BIN/ISO/EXE first, then retry.";
            }
            return fail(errorStream.str());
        }
        detail::appendDiagnostics(diagnostics, warnings, exeDiagnostics, activeDiscPath);

        ExeCandidateInfo candidate;
        candidate.path = activeDiscPath;
        candidate.valid = true;
        candidate.loadAddress = exeImage.header.loadAddress;
        candidate.loadSize = exeImage.header.loadSize;
        candidate.entryPoint = exeImage.entryPoint.pc;
        candidate.hash = detail::formatHex(detail::fnv1a64(exeImage.programData), 16);
        result.exeCandidates.push_back(candidate);
        result.selectionInfo.selectedPath = activeDiscPath;
        result.selectionInfo.reason = "Single executable input.";
    }

    if (exeImage.programData.empty())
    {
        return fail("Executable program data is empty.");
    }

    const Address baseAddress = exeImage.header.loadAddress;
    const auto disassembled = disasm::MipsDisassembler::disassemble(
        exeImage.programData.data(), exeImage.programData.size(), baseAddress);
    if (disassembled.empty())
    {
        return fail("Disassembler produced no instructions.");
    }

    const Address entryAddress = detail::resolveEntryAddress(exeImage);
    const auto jumpTables = disasm::findJumpTables(disassembled);
    const auto segmentation = disasm::segmentCodeAndData(disassembled, {entryAddress}, jumpTables);

    auto isInCodeRange = [&](Address address)
    {
        for (const auto& range : segmentation.codeRanges)
        {
            if (address >= range.start && address <= range.end)
            {
                return true;
            }
        }
        return false;
    };

    std::vector<disasm::Instruction> codeInstructions;
    codeInstructions.reserve(disassembled.size());
    for (const auto& instruction : disassembled)
    {
        if (isInCodeRange(instruction.address))
        {
            codeInstructions.push_back(instruction);
        }
    }

    if (codeInstructions.empty())
    {
        return fail("No reachable code instructions were identified from entrypoint traversal.");
    }

    auto boundaries = disasm::findFunctionBoundaries(codeInstructions);
    if (boundaries.empty())
    {
        boundaries.push_back({entryAddress, codeInstructions.back().address, false, false});
    }
    bool entryFound = false;
    for (const auto& boundary : boundaries)
    {
        if (boundary.start == entryAddress)
        {
            entryFound = true;
            break;
        }
    }
    if (!entryFound)
    {
        boundaries.push_back({entryAddress, codeInstructions.back().address, false, false});
    }
    std::sort(
        boundaries.begin(), boundaries.end(),
        [entryAddress](const disasm::FunctionBoundary& lhs, const disasm::FunctionBoundary& rhs)
        {
            const bool lhsIsEntry = lhs.start == entryAddress;
            const bool rhsIsEntry = rhs.start == entryAddress;
            if (lhsIsEntry != rhsIsEntry)
            {
                return lhsIsEntry;
            }
            return lhs.start < rhs.start;
        });

    if (!segmentation.dataRanges.empty())
    {
        PipelineDiagnostic segmentationNote;
        segmentationNote.code = "CodeDataSegmentation";
        segmentationNote.severity = "info";
        segmentationNote.message =
            "Code/data segmentation excluded non-reachable regions from instruction lowering.";
        segmentationNote.context.file = activeDiscPath;
        diagnostics.push_back(std::move(segmentationNote));
    }

    ir::Program program;
    for (const auto& boundary : boundaries)
    {
        std::vector<disasm::Instruction> functionInstructions;
        auto rangeBegin =
            std::lower_bound(codeInstructions.begin(), codeInstructions.end(), boundary.start,
                             [](const disasm::Instruction& instruction, Address target)
                             { return instruction.address < target; });
        auto rangeEnd = std::upper_bound(rangeBegin, codeInstructions.end(), boundary.end,
                                         [](Address target, const disasm::Instruction& instruction)
                                         { return target < instruction.address; });
        functionInstructions.insert(functionInstructions.end(), rangeBegin, rangeEnd);
        if (functionInstructions.empty())
        {
            PipelineDiagnostic entry;
            entry.code = "FunctionSkipped";
            entry.severity = "warning";
            entry.message = "No instructions found for function boundary.";
            entry.context.file = activeDiscPath;
            diagnostics.push_back(entry);
            warnings.push_back(entry.message);
            continue;
        }

        const std::string functionName = "func_0x" + detail::formatHex(boundary.start, 8);
        auto irBuild = ir::buildIrFromMips(functionInstructions);
        warnings.insert(warnings.end(), irBuild.warnings.begin(), irBuild.warnings.end());
        for (const auto& warning : irBuild.warnings)
        {
            PipelineDiagnostic entry;
            entry.code = "IrWarning";
            entry.severity = "warning";
            entry.message = warning;
            entry.context.file = activeDiscPath;
            diagnostics.push_back(std::move(entry));
        }

        if (!irBuild.errors.empty())
        {
            std::ostringstream stream;
            stream << "IR build failed for " << functionName << ":\n";
            for (const auto& error : irBuild.errors)
            {
                stream << " - " << error << "\n";
            }
            return fail(stream.str());
        }

        auto flowResult =
            ir::buildControlFlowFunction(functionName, boundary.start, irBuild.instructions);
        if (!flowResult.errors.empty())
        {
            std::ostringstream stream;
            stream << "Control-flow build failed for " << functionName << ":\n";
            for (const auto& error : flowResult.errors)
            {
                stream << " - " << error << "\n";
            }
            return fail(stream.str());
        }

        PipelineResult::FunctionMetadata metadataEntry;
        metadataEntry.name = functionName;
        metadataEntry.entryAddress = boundary.start;
        metadataEntry.endAddress = boundary.end;
        metadataEntry.hasPrologue = boundary.hasPrologue;
        metadataEntry.hasEpilogue = boundary.hasEpilogue;
        for (const auto& instruction : functionInstructions)
        {
            if (instruction.opcode == disasm::Opcode::JAL)
            {
                if (auto target = instruction.getJumpTarget())
                {
                    metadataEntry.directCalls.push_back(*target);
                }
            }
            else if (instruction.opcode == disasm::Opcode::JALR)
            {
                metadataEntry.indirectCallCount += 1;
            }
        }
        std::sort(metadataEntry.directCalls.begin(), metadataEntry.directCalls.end());
        metadataEntry.directCalls.erase(
            std::unique(metadataEntry.directCalls.begin(), metadataEntry.directCalls.end()),
            metadataEntry.directCalls.end());
        result.functions.push_back(std::move(metadataEntry));

        program.functions.push_back(std::move(flowResult.function));
    }

    if (program.functions.empty())
    {
        return fail("No functions were generated from the disassembly.");
    }

    if (m_options.enableOptimizations)
    {
        ir::runOptimizations(program);
    }

    CodeGenOptions codegenOptions;
    codegenOptions.enableOptimizations = m_options.enableOptimizations;
    codegenOptions.preserveSymbols = m_options.preserveSymbols;
    CodeGenerator codeGenerator(codegenOptions);

    const std::string inputStem =
        inputFsPath.stem().string().empty() ? "psx_module" : inputFsPath.stem().string();
    const std::string exeHash = detail::formatHex(detail::fnv1a64(exeImage.programData), 16);
    const std::string exeTag = detail::makeDeterministicTag(exeImage.header.loadAddress, exeHash);
    const std::string moduleName =
        detail::sanitizeModuleName(inputStem + "_" + detail::sanitizeModuleName(exeTag));

    for (auto& diagnostic : diagnostics)
    {
        if (diagnostic.context.module.empty())
        {
            diagnostic.context.module = moduleName;
        }
    }

    ModuleMetadata metadata;
    metadata.discSetName = result.discSet.setName;
    metadata.activeDiscIndex = result.discSet.activeDiscIndex;
    metadata.warnings = warnings;
    for (const auto& disc : result.discSet.discs)
    {
        ModuleMetadata::DiscEntry entry;
        entry.index = disc.discIndex;
        entry.label = disc.volumeLabel;
        entry.path = disc.path;
        metadata.discs.push_back(std::move(entry));
    }

    const std::string header = codeGenerator.generateHeader(program, moduleName);
    const std::string source = codeGenerator.generateSource(program, moduleName, metadata);
    const std::string buildFile = codeGenerator.generateBuildFile(moduleName);
    const std::string runnerSource = codeGenerator.generateRunnerSource(moduleName);

    std::filesystem::path outputDir =
        detail::buildOutputDirectory(std::filesystem::path(m_options.outputDirectory), inputStem,
                                     result.discSet.setName, exeTag);
    std::error_code dirError;
    std::filesystem::create_directories(outputDir, dirError);
    if (dirError)
    {
        return fail("Failed to create output directory: " + outputDir.string());
    }

    PipelineArtifacts artifacts;
    artifacts.moduleName = moduleName;
    artifacts.headerPath = (outputDir / (moduleName + ".h")).string();
    artifacts.sourcePath = (outputDir / (moduleName + ".cpp")).string();
    const std::string runnerPath = (outputDir / (moduleName + "_runner.cpp")).string();
    artifacts.buildPath = (outputDir / "CMakeLists.txt").string();
    artifacts.manifestPath = (outputDir / "manifest.json").string();

    artifacts.resourcesPath = (outputDir / "resources").string();
    artifacts.runtimeIncludePath = (outputDir / "runtime" / "include").string();
    artifacts.runtimeSourcePath = (outputDir / "runtime" / "src").string();

    std::string writeError;
    if (!detail::writeFile(artifacts.headerPath, header, writeError) ||
        !detail::writeFile(artifacts.sourcePath, source, writeError) ||
        !detail::writeFile(runnerPath, runnerSource, writeError) ||
        !detail::writeFile(artifacts.buildPath, buildFile, writeError))
    {
        return fail(writeError);
    }

    std::filesystem::path repoRoot =
        detail::repositoryRootFromSourcePath(std::filesystem::path(__FILE__));
    if (!detail::copyDirectoryRecursive(
            repoRoot / "include" / "psxrecomp",
            std::filesystem::path(artifacts.runtimeIncludePath) / "psxrecomp", writeError) ||
        !detail::copyDirectoryRecursive(repoRoot / "src" / "runtime", artifacts.runtimeSourcePath,
                                        writeError))
    {
        return fail(writeError);
    }

    if (detail::isIsoLikePath(inputFsPath))
    {
        iso::IsoParser parser(activeDiscPath);
        if (parser.open() && parser.isValid())
        {
            std::filesystem::create_directories(artifacts.resourcesPath, dirError);
            if (dirError)
            {
                return fail("Failed to create resources directory: " + artifacts.resourcesPath);
            }
            std::vector<std::pair<iso::ResourceType, std::string>> resourceTypes = {
                {iso::ResourceType::TimTexture, "TIM"},
                {iso::ResourceType::StrVideo, "STR"},
                {iso::ResourceType::XaAudio, "XA"},
            };
            for (const auto& [type, label] : resourceTypes)
            {
                auto listed = parser.listResources(type);
                for (const auto& resourcePath : listed)
                {
                    artifacts.exportedResources.push_back(resourcePath);
                }
                if (!parser.exportResources(type, artifacts.resourcesPath))
                {
                    warnings.push_back("Resource export reported errors for " + label + ".");
                }
            }
            std::sort(artifacts.exportedResources.begin(), artifacts.exportedResources.end());
            artifacts.exportedResources.erase(
                std::unique(artifacts.exportedResources.begin(), artifacts.exportedResources.end()),
                artifacts.exportedResources.end());
        }
        else
        {
            appendIsoParserErrors(parser, "Resource export skipped");
            warnings.push_back("Resource export skipped due to parser errors.");
        }
    }

    result.success = true;
    result.artifacts = artifacts;
    result.warnings = warnings;
    result.diagnostics = diagnostics;

    const std::string timestamp = detail::buildTimestamp(m_options.manifestTimestamp);
    const std::string manifest = detail::serializeManifest(
        result, activeDiscPath, outputDir.string(), timestamp, m_options.pipelineVersion);
    if (!detail::writeFile(artifacts.manifestPath, manifest, writeError))
    {
        return fail(writeError);
    }
    return result;
}

} // namespace recompiler
} // namespace psxrecomp
