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
#include <unordered_set>

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

std::vector<std::string> deduplicatePathsCaseInsensitive(const std::vector<std::string>& paths)
{
    std::vector<std::string> normalized;
    std::vector<std::string> uniquePaths;
    for (const auto& path : paths)
    {
        std::string key = detail::toLower(path);
        if (std::find(normalized.begin(), normalized.end(), key) == normalized.end())
        {
            normalized.push_back(std::move(key));
            uniquePaths.push_back(path);
        }
    }
    return uniquePaths;
}

bool exeCandidateLess(const ExeCandidateInfo& lhs, const ExeCandidateInfo& rhs)
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
}

std::optional<size_t> selectExeCandidateIndex(const std::vector<ExeCandidateInfo>& candidates,
                                              const std::string& bootPath,
                                              std::string& selectionReason)
{
    if (!bootPath.empty())
    {
        const std::string loweredBootPath = detail::toLower(bootPath);
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            const auto& candidate = candidates[index];
            if (candidate.valid && detail::toLower(candidate.path) == loweredBootPath)
            {
                selectionReason = "Selected SYSTEM.CNF BOOT candidate.";
                return index;
            }
        }
    }

    for (size_t index = 0; index < candidates.size(); ++index)
    {
        if (candidates[index].valid)
        {
            selectionReason = "Selected first valid candidate after sorting.";
            return index;
        }
    }

    return std::nullopt;
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
        const std::vector<std::string> uniquePaths =
            deduplicatePathsCaseInsensitive(candidatePaths);

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

        std::sort(result.exeCandidates.begin(), result.exeCandidates.end(), exeCandidateLess);
        std::optional<size_t> selectedIndex =
            selectExeCandidateIndex(result.exeCandidates, bootPath, result.selectionInfo.reason);
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

    // Collect all JAL targets from the raw disassembly as additional entry
    // point seeds.  This ensures that functions reachable only through
    // indirect calls (JALR) — whose callers ARE reachable — are still
    // marked as code even though the BFS in segmentCodeAndData cannot
    // follow register-based targets.
    //
    // We filter targets to the binary's address range to avoid garbage
    // targets from data words that happen to decode as JAL instructions.
    const Address binaryEnd = baseAddress + static_cast<Address>(exeImage.programData.size());
    std::vector<Address> entrySeeds = {entryAddress};
    for (const auto& instruction : disassembled)
    {
        if (instruction.opcode == disasm::Opcode::JAL ||
            instruction.opcode == disasm::Opcode::BGEZAL ||
            instruction.opcode == disasm::Opcode::BLTZAL)
        {
            auto target = instruction.getTargetAddress();
            if (target.has_value() && *target >= baseAddress && *target < binaryEnd &&
                (*target % 4) == 0)
            {
                entrySeeds.push_back(*target);
            }
        }
    }

    auto segmentation = disasm::segmentCodeAndData(disassembled, entrySeeds, jumpTables);

    // Harvest potential code pointers from DATA regions.  After the initial
    // segmentation, scan every 4-byte aligned word in data regions and check
    // whether it looks like a valid code address (i.e. falls within the
    // binary's address range and is 4-byte aligned).  These are likely
    // function pointer tables, vtables, or callback registrations that
    // the BFS cannot follow because they are only used via JALR at runtime.
    // We restrict to data regions to avoid false positives from code that
    // happens to contain address-like immediate values.
    std::vector<Address> harvestedPointers;
    {
        const Address endAddress = baseAddress + static_cast<Address>(exeImage.programData.size());
        for (const auto& range : segmentation.dataRanges)
        {
            for (Address addr = range.start; addr <= range.end; addr += 4)
            {
                const size_t offset = addr - baseAddress;
                if (offset + 4 <= exeImage.programData.size())
                {
                    const uint32_t value =
                        static_cast<uint32_t>(exeImage.programData[offset]) |
                        (static_cast<uint32_t>(exeImage.programData[offset + 1]) << 8) |
                        (static_cast<uint32_t>(exeImage.programData[offset + 2]) << 16) |
                        (static_cast<uint32_t>(exeImage.programData[offset + 3]) << 24);
                    if (value >= baseAddress && value < endAddress && (value % 4) == 0)
                    {
                        harvestedPointers.push_back(value);
                    }
                }
            }
        }
        if (!harvestedPointers.empty())
        {
            // Add harvested pointers as BFS entry seeds and re-run
            // segmentation so those code regions are properly classified.
            for (const Address ptr : harvestedPointers)
            {
                entrySeeds.push_back(ptr);
            }
            segmentation = disasm::segmentCodeAndData(disassembled, entrySeeds, jumpTables);
        }
    }

    // Harvest code-address construction patterns (LUI + ADDIU/ORI).
    // PSn00bSDK (and similar libraries) initialise function pointer tables
    // at runtime via sequences such as:
    //   LUI  $reg, HI         ; 0x3Crrxxxx
    //   ADDIU $reg, $reg, LO  ; 0x24rrxxxx  (or ORI, 0x34rrxxxx)
    //   SW   $reg, offset($base)
    // The final address = (HI << 16) + sign_extend(LO) for ADDIU, or
    //                     (HI << 16) | LO            for ORI.
    // We scan all disassembled instructions for LUI followed by a matching
    // ADDIU/ORI within 3 instructions (allowing an intervening NOP or
    // other unrelated instruction), compute the resulting address, and add
    // it as an entry seed if it falls within the binary code range.
    std::vector<Address> codeHarvestedPointers;
    {
        const Address endAddress = baseAddress + static_cast<Address>(exeImage.programData.size());
        std::unordered_set<Address> existingSeeds(entrySeeds.begin(), entrySeeds.end());
        size_t harvestedFromCode = 0;

        for (size_t i = 0; i < disassembled.size(); ++i)
        {
            const auto& inst = disassembled[i];
            // LUI: opcode field = 001111 (0x0F), bits [31:26]
            if (inst.opcode != disasm::Opcode::LUI)
            {
                continue;
            }
            const u8 luiReg = inst.rt;                  // destination register
            const u32 hiImm = inst.immediate & 0xFFFFu; // upper 16-bit immediate

            // Scan the next 6 instructions for a matching ADDIU or ORI.
            // PSn00bSDK patterns often have 4-5 instructions between LUI and
            // ADDIU (e.g., delay slot of a J instruction).
            for (size_t j = 1; j <= 6 && i + j < disassembled.size(); ++j)
            {
                const auto& next = disassembled[i + j];
                // ADDIU: rt == rs == luiReg
                if (next.opcode == disasm::Opcode::ADDIU && next.rs == luiReg && next.rt == luiReg)
                {
                    const u32 lo = next.immediate & 0xFFFFu;
                    const s32 slo =
                        (lo & 0x8000u) ? static_cast<s32>(lo | 0xFFFF0000u) : static_cast<s32>(lo);
                    const Address addr =
                        static_cast<Address>((hiImm << 16) + static_cast<u32>(slo));
                    if (addr >= baseAddress && addr < endAddress && (addr % 4) == 0 &&
                        existingSeeds.find(addr) == existingSeeds.end())
                    {
                        entrySeeds.push_back(addr);
                        existingSeeds.insert(addr);
                        codeHarvestedPointers.push_back(addr);
                        ++harvestedFromCode;
                    }
                    break;
                }
                // ORI: rt == rs == luiReg
                if (next.opcode == disasm::Opcode::ORI && next.rs == luiReg && next.rt == luiReg)
                {
                    const u32 lo = next.immediate & 0xFFFFu;
                    const Address addr = static_cast<Address>((hiImm << 16) | lo);
                    if (addr >= baseAddress && addr < endAddress && (addr % 4) == 0 &&
                        existingSeeds.find(addr) == existingSeeds.end())
                    {
                        entrySeeds.push_back(addr);
                        existingSeeds.insert(addr);
                        codeHarvestedPointers.push_back(addr);
                        ++harvestedFromCode;
                    }
                    break;
                }
                // If another LUI to the same register appears, stop scanning.
                if (next.opcode == disasm::Opcode::LUI && next.rt == luiReg)
                {
                    break;
                }
            }
        }

        if (harvestedFromCode > 0)
        {
            // Re-run segmentation with the expanded seed set so newly
            // discovered code regions are properly classified.
            segmentation = disasm::segmentCodeAndData(disassembled, entrySeeds, jumpTables);
        }
    }

    // Build code-only instruction set from segmentation results.
    // This excludes data regions (string literals, padding, vtables)
    // so that the disassembler doesn't try to decode non-instruction bytes.
    std::vector<disasm::Instruction> codeInstructions;
    {
        std::unordered_set<Address> codeAddresses;
        for (const auto& range : segmentation.codeRanges)
        {
            for (Address addr = range.start; addr <= range.end; addr += 4)
            {
                codeAddresses.insert(addr);
            }
        }
        codeInstructions.reserve(codeAddresses.size());
        for (const auto& instruction : disassembled)
        {
            if (codeAddresses.count(instruction.address))
            {
                codeInstructions.push_back(instruction);
            }
        }
    }

    if (codeInstructions.empty())
    {
        return fail("No reachable code instructions were identified from entrypoint traversal.");
    }

    // Pass harvested pointers AND the EXE entry point as additional
    // function start addresses.  The entry point often lacks a standard
    // ADDIU SP,-N prologue (e.g. NOP + JR RA stubs) and is not a JAL
    // target, so findFunctionBoundaries won't discover it on its own.
    // Including it as an additional start lets the boundary finder scan
    // for JR RA from the entry address and produce a correctly-sized
    // boundary instead of a massive catch-all.
    // Also include addresses harvested from LUI+ADDIU/ORI code patterns
    // (codeHarvestedPointers) — these are runtime-constructed function
    // pointers (e.g. BIOS callback addresses) that are not direct JAL
    // targets and would otherwise be missed by boundary detection.
    std::vector<Address> additionalStarts = harvestedPointers;
    additionalStarts.insert(additionalStarts.end(), codeHarvestedPointers.begin(),
                            codeHarvestedPointers.end());
    additionalStarts.push_back(entryAddress);

    auto boundaries = disasm::findFunctionBoundaries(codeInstructions, additionalStarts);
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
        // Entry address was not in the code instruction stream — create a
        // minimal boundary so the runner can dispatch to it.  Use the
        // address itself as both start and end rather than spanning the
        // entire binary.
        boundaries.push_back({entryAddress, entryAddress, false, false});
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

    // Fill gaps between function boundaries with synthetic functions so that
    // no instruction range is orphaned.  With proper code/data segmentation
    // and code-pointer harvesting, gaps between recognized functions are
    // genuine code regions (leaf functions called via JALR, etc.) that
    // findFunctionBoundaries missed because they lack standard prologues.
    {
        std::vector<disasm::FunctionBoundary> filledBoundaries;
        filledBoundaries.reserve(boundaries.size() * 2);

        for (size_t i = 0; i < boundaries.size(); ++i)
        {
            filledBoundaries.push_back(boundaries[i]);

            if (i + 1 < boundaries.size())
            {
                const Address gapStart = boundaries[i].end + 4;
                const Address gapEnd = boundaries[i + 1].start - 4;
                if (gapStart <= gapEnd)
                {
                    filledBoundaries.push_back({gapStart, gapEnd, false, false});
                }
            }
        }

        // Also fill the trailing gap between the last recognized function
        // and the end of the code instruction stream.  Without this,
        // functions discovered only through LUI+ADDIU pointer harvesting
        // (e.g. BIOS callback targets) that reside after the last
        // statically-called function would be omitted from recompilation.
        if (!filledBoundaries.empty() && !codeInstructions.empty())
        {
            const Address lastEnd = filledBoundaries.back().end;
            const Address codeEnd = codeInstructions.back().address;
            if (lastEnd + 4 <= codeEnd)
            {
                filledBoundaries.push_back({lastEnd + 4, codeEnd, false, false});
            }
        }

        boundaries = std::move(filledBoundaries);
    }

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
