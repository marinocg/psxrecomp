#include "psxrecomp/recompiler/pipeline.h"

#include "pipeline_analysis_helpers.h"
#include "pipeline_helpers.h"
#include "pipeline_input_loader.h"
#include "pipeline_selection_helpers.h"
#include "pipeline_validation.h"
#include "psxrecomp/disasm/analysis.h"
#include "psxrecomp/disasm/instruction.h"
#include "psxrecomp/ir/control_flow.h"
#include "psxrecomp/ir/mips_ir_builder.h"
#include "psxrecomp/ir/optimizations.h"
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
    std::optional<detail::ResourceWorkspaceInfo> workspaceInfo;
    std::vector<std::string> warnings;
    std::vector<PipelineDiagnostic> diagnostics;
    PipelineResult result;
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

    std::string loadError;
    if (!detail::loadExecutableImageFromInput(activeDiscPath, inputFsPath, result, workspaceInfo,
                                              warnings, diagnostics, exeImage, loadError))
    {
        return fail(loadError);
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

    const std::unordered_set<Address> speculativeSeeds(
        codeLayout.speculativeFunctionEntries.begin(),
        codeLayout.speculativeFunctionEntries.end());
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
            if (speculativeSeeds.count(adjustedBoundary.start) != 0)
            {
                bool hasDelaySlotError = false;
                for (const auto& err : irBuild.errors)
                {
                    if (err.find("Missing delay-slot instruction") != std::string::npos)
                    {
                        hasDelaySlotError = true;
                        break;
                    }
                }
                if (hasDelaySlotError)
                {
                    PipelineDiagnostic pruneDiag;
                    pruneDiag.code = "SpuriousHarvestedSeed";
                    pruneDiag.severity = "warning";
                    pruneDiag.message =
                        "Speculative seed pruned (delay-slot IR error): " + functionName;
                    pruneDiag.context.file = activeDiscPath;
                    diagnostics.push_back(std::move(pruneDiag));
                    warnings.push_back("Speculative seed pruned: " + functionName);
                    continue;
                }
            }
            std::ostringstream stream;
            stream << "IR build failed for " << functionName << ":\n";
            for (const auto& error : irBuild.errors)
            {
                stream << " - " << error << "\n";
            }
            return fail(stream.str());
        }

        if (irBuild.instructions.empty())
        {
            PipelineDiagnostic entry;
            entry.code = "FunctionSkipped";
            entry.severity = "warning";
            entry.message = "No IR instructions emitted for function boundary.";
            entry.context.file = activeDiscPath;
            diagnostics.push_back(entry);
            warnings.push_back(entry.message + " @ " + functionName);
            continue;
        }

        auto flowResult = ir::buildControlFlowFunction(functionName, adjustedBoundary.start,
                                                       irBuild.instructions);
        if (!flowResult.errors.empty())
        {
            const bool isSpeculative =
                speculativeSeeds.count(adjustedBoundary.start) != 0;
            bool hasEntryNotFound = false;
            for (const auto& err : flowResult.errors)
            {
                if (err.find("Entry address not found") != std::string::npos)
                    hasEntryNotFound = true;
            }
            if (isSpeculative && hasEntryNotFound)
            {
                PipelineDiagnostic pruneDiag;
                pruneDiag.code = "SpuriousHarvestedSeed";
                pruneDiag.severity = "warning";
                pruneDiag.message =
                    "Speculative seed pruned (entry not in IR): " + functionName;
                pruneDiag.context.file = activeDiscPath;
                diagnostics.push_back(std::move(pruneDiag));
                warnings.push_back("Speculative seed pruned: " + functionName);
                continue;
            }
            std::ostringstream stream;
            stream << "Control-flow build failed for " << functionName << ":\n";
            for (const auto& error : flowResult.errors)
            {
                stream << " - " << error << "\n";
            }
            return fail(stream.str());
        }

        if (auto verifyError = detail::verifyFunctionForCodegen(flowResult.function, activeDiscPath,
                                                                "post-cfg", diagnostics))
        {
            return fail(*verifyError);
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
        for (const auto& function : program.functions)
        {
            if (auto verifyError = detail::verifyFunctionForCodegen(
                    function, activeDiscPath, "post-optimization", diagnostics))
            {
                return fail(*verifyError);
            }
        }
    }

    for (const auto& function : program.functions)
    {
        if (auto verifyError = detail::verifyFunctionForCodegen(function, activeDiscPath,
                                                                "pre-codegen", diagnostics))
        {
            return fail(*verifyError);
        }
    }

    CodeGenOptions codegenOptions;
    codegenOptions.enableOptimizations = m_options.enableOptimizations;
    codegenOptions.preserveSymbols = m_options.preserveSymbols;
    CodeGenerator codeGenerator(codegenOptions);

    std::filesystem::path inputStemPath = inputFsPath;
    if (workspaceInfo.has_value())
    {
        if (!workspaceInfo->discMetaInputPath.empty())
        {
            inputStemPath = std::filesystem::path(workspaceInfo->discMetaInputPath);
        }
        else if (!result.selectionInfo.selectedPath.empty())
        {
            inputStemPath = std::filesystem::path(result.selectionInfo.selectedPath);
        }
    }
    const std::string inputStem =
        inputStemPath.stem().string().empty() ? "psx_module" : inputStemPath.stem().string();
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
    metadata.harvestedFunctionEntries = codeLayout.harvestedFunctionEntries;
    metadata.knownPointerTableWords = codeLayout.knownPointerTableWords;
    for (const auto& site : codeLayout.indirectCallSites)
    {
        ModuleMetadata::IndirectCallSiteEntry entry;
        entry.callerPc = site.callerPc;
        entry.containingFunction = site.containingFunction;
        entry.pointerWordAddress = site.pointerWordAddress;
        entry.sourceRegister = site.sourceRegister;
        entry.pointerBaseRegister = site.pointerBaseRegister;
        entry.pointerOffset = site.pointerOffset;
        entry.hasStaticPointerWordAddress = site.hasStaticPointerWordAddress;
        entry.pointerLoadClobbersBase = site.pointerLoadClobbersBase;
        metadata.indirectCallSites.push_back(entry);
    }
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

    std::string header;
    std::string source;
    std::string buildFile;
    std::string runnerSource;
    try
    {
        header = codeGenerator.generateHeader(program, moduleName);
        source = codeGenerator.generateSource(program, moduleName, metadata);
        buildFile = codeGenerator.generateBuildFile(moduleName);
        runnerSource = codeGenerator.generateRunnerSource(moduleName);
    }
    catch (const std::exception& exception)
    {
        PipelineDiagnostic entry;
        entry.code = "CodegenFailure";
        entry.severity = "error";
        entry.message = std::string("Code generation failed: ") + exception.what();
        entry.context.file = activeDiscPath;
        diagnostics.push_back(std::move(entry));
        return fail(std::string("Code generation failed: ") + exception.what());
    }

    std::filesystem::path outputDir =
        detail::buildOutputDirectory(std::filesystem::path(m_options.outputDirectory), inputStem,
                                     result.discSet.setName, exeTag);

    const std::string resourceWorkspacePath =
        workspaceInfo.has_value() ? workspaceInfo->workspaceRoot.string() : "";
    std::string writeError;
    if (!detail::writeOutputArtifacts(
            result, outputDir, moduleName, header, source, runnerSource, buildFile, activeDiscPath,
            inputFsPath, resourceWorkspacePath, m_options.resourceExport, warnings, diagnostics,
            m_options.manifestTimestamp, m_options.pipelineVersion, writeError))
    {
        return fail(writeError);
    }

    return result;
}

} // namespace recompiler
} // namespace psxrecomp
