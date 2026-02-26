#include "psxrecomp/recompiler/pipeline.h"

#include "pipeline_analysis_helpers.h"
#include "pipeline_helpers.h"
#include "pipeline_selection_helpers.h"
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
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{

RecompilationPipeline::RecompilationPipeline(PipelineOptions options)
    : m_options(std::move(options))
{
}

PipelineResult RecompilationPipeline::run(const std::string& inputPath)
{
    if (inputPath.empty())
    {
        return detail::buildPipelineError("Input path is empty.", {}, {}, {});
    }

    if (m_options.outputDirectory.empty())
    {
        return detail::buildPipelineError("Output directory is not set.", {}, {}, {});
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
            detail::buildPipelineError(message, warnings, diagnostics, result.exeCandidates);
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
        const std::vector<std::string> uniquePaths =
            detail::deduplicatePathsCaseInsensitive(candidatePaths);

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

        std::sort(result.exeCandidates.begin(), result.exeCandidates.end(),
                  detail::exeCandidateLess);
        std::optional<size_t> selectedIndex = detail::selectExeCandidateIndex(
            result.exeCandidates, bootPath, result.selectionInfo.reason);
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
    const detail::PipelineCodeLayout codeLayout =
        detail::analyzeCodeLayout(disassembled, exeImage, baseAddress, entryAddress);
    if (codeLayout.codeInstructions.empty())
    {
        return fail("No reachable code instructions were identified from entrypoint traversal.");
    }

    if (!codeLayout.segmentation.dataRanges.empty())
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
    for (const auto& boundary : codeLayout.boundaries)
    {
        std::vector<disasm::Instruction> functionInstructions;
        auto rangeBegin = std::lower_bound(
            codeLayout.codeInstructions.begin(), codeLayout.codeInstructions.end(), boundary.start,
            [](const disasm::Instruction& instruction, Address target)
            { return instruction.address < target; });
        auto rangeEnd =
            std::upper_bound(rangeBegin, codeLayout.codeInstructions.end(), boundary.end,
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

        // For gap-filled boundaries, the gap start address may fall in a
        // data region that has no code instructions.  Adjust the boundary
        // start to the first actual instruction address so that the
        // control-flow builder can find its entry point.
        disasm::FunctionBoundary adjustedBoundary = boundary;
        if (functionInstructions.front().address != boundary.start)
        {
            adjustedBoundary.start = functionInstructions.front().address;
        }

        const std::string functionName = "func_0x" + detail::formatHex(adjustedBoundary.start, 8);

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

        auto flowResult = ir::buildControlFlowFunction(functionName, adjustedBoundary.start,
                                                       irBuild.instructions);
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
        metadataEntry.entryAddress = adjustedBoundary.start;
        metadataEntry.endAddress = adjustedBoundary.end;
        metadataEntry.hasPrologue = adjustedBoundary.hasPrologue;
        metadataEntry.hasEpilogue = adjustedBoundary.hasEpilogue;
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
    metadata.entryAddress = entryAddress;
    metadata.initialGp = exeImage.entryPoint.gp;
    metadata.stackAddress = exeImage.header.stackAddress;
    metadata.stackSize = exeImage.header.stackSize;
    metadata.loadAddress = exeImage.header.loadAddress;
    metadata.loadSize = exeImage.header.loadSize;
    metadata.programData = exeImage.programData;
    metadata.warnings = warnings;
    for (const auto& disc : result.discSet.discs)
    {
        ModuleMetadata::DiscEntry entry;
        entry.index = disc.discIndex;
        entry.label = disc.volumeLabel;
        std::filesystem::path discPath(disc.path);
        const std::string discFileName = discPath.filename().string();
        entry.path = discFileName.empty() ? disc.path : discFileName;
        metadata.discs.push_back(std::move(entry));
    }

    const std::string header = codeGenerator.generateHeader(program, moduleName);
    const std::string source = codeGenerator.generateSource(program, moduleName, metadata);
    const std::string buildFile = codeGenerator.generateBuildFile(moduleName);
    const std::string runnerSource = codeGenerator.generateRunnerSource(moduleName);

    std::filesystem::path outputDir =
        detail::buildOutputDirectory(std::filesystem::path(m_options.outputDirectory), inputStem,
                                     result.discSet.setName, exeTag);

    std::string writeError;
    if (!detail::writeOutputArtifacts(result, outputDir, moduleName, header, source, runnerSource,
                                      buildFile, activeDiscPath, inputFsPath, warnings, diagnostics,
                                      m_options.manifestTimestamp, m_options.pipelineVersion,
                                      writeError))
    {
        return fail(writeError);
    }

    return result;
}

} // namespace recompiler
} // namespace psxrecomp
