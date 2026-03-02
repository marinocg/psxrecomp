#include "pipeline_input_loader.h"

#include "pipeline_helpers.h"
#include "pipeline_selection_helpers.h"
#include "psxrecomp/iso/iso_boot.h"
#include "psxrecomp/iso/iso_parser.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

bool loadExecutableImageFromInput(const std::string& activeDiscPath,
                                  const std::filesystem::path& inputFsPath, PipelineResult& result,
                                  std::optional<ResourceWorkspaceInfo>& outWorkspaceInfo,
                                  std::vector<std::string>& warnings,
                                  std::vector<PipelineDiagnostic>& diagnostics,
                                  iso::PsxExeImage& outExeImage, std::string& outError)
{
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

    auto fail = [&](std::string message)
    {
        outError = std::move(message);
        return false;
    };

    std::filesystem::path workspaceRoot;
    if (detectResourceWorkspaceRoot(inputFsPath, workspaceRoot))
    {
        ResourceWorkspaceInfo loadedWorkspaceInfo;
        std::string workspaceError;
        if (!loadResourceWorkspaceInfo(workspaceRoot, loadedWorkspaceInfo, workspaceError))
        {
            return fail("Failed to parse resources workspace: " + workspaceError);
        }
        outWorkspaceInfo = std::move(loadedWorkspaceInfo);
        if (!outWorkspaceInfo->discMetaVolumeLabel.empty())
        {
            result.discSet.setName = outWorkspaceInfo->discMetaVolumeLabel;
            if (!result.discSet.discs.empty())
            {
                result.discSet.discs.front().volumeLabel = outWorkspaceInfo->discMetaVolumeLabel;
            }
        }
        if (!outWorkspaceInfo->discMetaInputPath.empty() && !result.discSet.discs.empty())
        {
            result.discSet.discs.front().path = outWorkspaceInfo->discMetaInputPath;
        }
    }

    if (outWorkspaceInfo.has_value())
    {
        std::unordered_map<std::string, std::filesystem::path> hostPathByIsoPath;
        std::unordered_set<std::string> seenIsoPaths;
        for (const auto& executableInfo : outWorkspaceInfo->executables)
        {
            const std::string isoPathKey = toLower(executableInfo.isoPath);
            if (!seenIsoPaths.insert(isoPathKey).second)
            {
                continue;
            }

            ExeCandidateInfo candidate;
            candidate.path = executableInfo.isoPath;
            std::filesystem::path executableHostPath;
            std::string resolveError;
            if (!resolveWorkspaceExecutableHostPath(*outWorkspaceInfo, executableInfo,
                                                    executableHostPath, resolveError))
            {
                PipelineDiagnostic entry;
                entry.code = "WorkspaceExecutablePathInvalid";
                entry.severity = "error";
                entry.message = "Invalid workspace executable path: " + resolveError;
                entry.context.file = candidate.path;
                diagnostics.push_back(entry);
                candidate.diagnostics.push_back(std::move(entry));
                result.exeCandidates.push_back(std::move(candidate));
                continue;
            }
            hostPathByIsoPath.emplace(isoPathKey, executableHostPath);

            std::error_code fileError;
            if (!std::filesystem::is_regular_file(executableHostPath, fileError) || fileError)
            {
                PipelineDiagnostic entry;
                entry.code = "WorkspaceExecutableMissing";
                entry.severity = "error";
                entry.message = "Workspace executable is missing: " + executableHostPath.string();
                entry.context.file = candidate.path;
                diagnostics.push_back(entry);
                candidate.diagnostics.push_back(std::move(entry));
                result.exeCandidates.push_back(std::move(candidate));
                continue;
            }

            iso::PsxExeDiagnostics exeDiagnostics;
            iso::PsxExeImage image{};
            if (!iso::PsxExeLoader::loadFromFile(executableHostPath.string(), image,
                                                 &exeDiagnostics))
            {
                appendDiagnostics(diagnostics, warnings, exeDiagnostics, candidate.path);
                for (const auto& entry : exeDiagnostics.entries)
                {
                    candidate.diagnostics.push_back(toPipelineDiagnostic(entry, candidate.path));
                }
                result.exeCandidates.push_back(std::move(candidate));
                continue;
            }

            candidate.valid = true;
            candidate.loadAddress = image.header.loadAddress;
            candidate.loadSize = image.header.loadSize;
            candidate.entryPoint = image.entryPoint.pc;
            candidate.hash = formatHex(fnv1a64(image.programData), 16);
            appendDiagnostics(diagnostics, warnings, exeDiagnostics, candidate.path);
            for (const auto& entry : exeDiagnostics.entries)
            {
                candidate.diagnostics.push_back(toPipelineDiagnostic(entry, candidate.path));
            }
            result.exeCandidates.push_back(std::move(candidate));
        }

        std::sort(result.exeCandidates.begin(), result.exeCandidates.end(), exeCandidateLess);
        const std::string bootPath = outWorkspaceInfo->bootIsoPath;
        std::optional<size_t> selectedIndex =
            selectExeCandidateIndex(result.exeCandidates, bootPath, result.selectionInfo.reason);
        if (!selectedIndex.has_value())
        {
            return fail("No valid PSX executable candidate found in resources workspace.");
        }

        const auto& selected = result.exeCandidates[selectedIndex.value()];
        result.selectionInfo.selectedPath = selected.path;
        auto hostPathIt = hostPathByIsoPath.find(toLower(selected.path));
        if (hostPathIt == hostPathByIsoPath.end())
        {
            return fail("Selected workspace executable mapping was not found.");
        }

        iso::PsxExeDiagnostics exeDiagnostics;
        if (!iso::PsxExeLoader::loadFromFile(hostPathIt->second.string(), outExeImage,
                                             &exeDiagnostics))
        {
            appendDiagnostics(diagnostics, warnings, exeDiagnostics, selected.path);
            return fail("Failed to parse selected PSX executable from resources workspace.");
        }
        appendDiagnostics(diagnostics, warnings, exeDiagnostics, selected.path);
        return true;
    }

    if (isIsoLikePath(inputFsPath))
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
                appendDiagnostics(diagnostics, warnings, exeDiagnostics, path);
                for (const auto& entry : exeDiagnostics.entries)
                {
                    candidate.diagnostics.push_back(toPipelineDiagnostic(entry, path));
                }
                result.exeCandidates.push_back(std::move(candidate));
                continue;
            }

            candidate.valid = true;
            candidate.loadAddress = image.header.loadAddress;
            candidate.loadSize = image.header.loadSize;
            candidate.entryPoint = image.entryPoint.pc;
            candidate.hash = formatHex(fnv1a64(image.programData), 16);
            appendDiagnostics(diagnostics, warnings, exeDiagnostics, path);
            for (const auto& entry : exeDiagnostics.entries)
            {
                candidate.diagnostics.push_back(toPipelineDiagnostic(entry, path));
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
        if (!iso::PsxExeLoader::loadImage(exeData, outExeImage, nullptr))
        {
            appendIsoParserErrors(parser, "Selected executable extraction failed");
            return fail("Failed to parse selected PSX executable.");
        }
        return true;
    }

    iso::PsxExeDiagnostics exeDiagnostics;
    if (!iso::PsxExeLoader::loadFromFile(activeDiscPath, outExeImage, &exeDiagnostics))
    {
        appendDiagnostics(diagnostics, warnings, exeDiagnostics, activeDiscPath);
        std::ostringstream errorStream;
        errorStream << "Failed to load PSX executable.";
        if (toLower(inputFsPath.extension().string()) == ".ecm")
        {
            errorStream << " Input appears to be ECM-compressed. Decode the image/executable to "
                           "BIN/ISO/EXE first, then retry.";
        }
        return fail(errorStream.str());
    }
    appendDiagnostics(diagnostics, warnings, exeDiagnostics, activeDiscPath);

    ExeCandidateInfo candidate;
    candidate.path = activeDiscPath;
    candidate.valid = true;
    candidate.loadAddress = outExeImage.header.loadAddress;
    candidate.loadSize = outExeImage.header.loadSize;
    candidate.entryPoint = outExeImage.entryPoint.pc;
    candidate.hash = formatHex(fnv1a64(outExeImage.programData), 16);
    result.exeCandidates.push_back(candidate);
    result.selectionInfo.selectedPath = activeDiscPath;
    result.selectionInfo.reason = "Single executable input.";
    return true;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
