#include "pipeline_helpers.h"

#include "psxrecomp/iso/iso_parser.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <utility>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{
std::string summarizePath(const std::string& value)
{
    if (value.empty())
    {
        return value;
    }

    std::filesystem::path path(value);
    const std::string filename = path.filename().string();
    return filename.empty() ? value : filename;
}

std::string escapeJson(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value)
    {
        switch (ch)
        {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

void appendManifestFunctions(std::ostringstream& stream, const PipelineResult& result)
{
    stream << "  \"functions\": [\n";
    for (size_t i = 0; i < result.functions.size(); ++i)
    {
        const auto& functionInfo = result.functions[i];
        stream << "    {\n";
        stream << "      \"name\": \"" << escapeJson(functionInfo.name) << "\",\n";
        stream << "      \"entryAddress\": \"0x" << formatHex(functionInfo.entryAddress, 8)
               << "\",\n";
        stream << "      \"endAddress\": \"0x" << formatHex(functionInfo.endAddress, 8) << "\",\n";
        stream << "      \"hasPrologue\": " << (functionInfo.hasPrologue ? "true" : "false")
               << ",\n";
        stream << "      \"hasEpilogue\": " << (functionInfo.hasEpilogue ? "true" : "false")
               << ",\n";
        stream << "      \"directCalls\": [";
        for (size_t callIndex = 0; callIndex < functionInfo.directCalls.size(); ++callIndex)
        {
            stream << "\"0x" << formatHex(functionInfo.directCalls[callIndex], 8) << "\"";
            if (callIndex + 1 < functionInfo.directCalls.size())
            {
                stream << ", ";
            }
        }
        stream << "],\n";
        stream << "      \"indirectCallCount\": " << functionInfo.indirectCallCount << "\n";
        stream << "    }";
        if (i + 1 < result.functions.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ],\n";
}

void appendManifestDiagnostics(std::ostringstream& stream, const PipelineResult& result)
{
    stream << "  \"diagnostics\": [\n";
    for (size_t i = 0; i < result.diagnostics.size(); ++i)
    {
        const auto& diag = result.diagnostics[i];
        stream << "    {\n";
        stream << "      \"code\": \"" << escapeJson(diag.code) << "\",\n";
        stream << "      \"severity\": \"" << escapeJson(diag.severity) << "\",\n";
        stream << "      \"message\": \"" << escapeJson(diag.message) << "\",\n";
        stream << "      \"context\": {\n";
        stream << "        \"file\": \"" << escapeJson(summarizePath(diag.context.file)) << "\",\n";
        stream << "        \"module\": \"" << escapeJson(diag.context.module) << "\"";
        if (diag.context.offset.has_value())
        {
            stream << ",\n        \"offset\": " << diag.context.offset.value() << "\n";
        }
        else
        {
            stream << "\n";
        }
        stream << "      }\n";
        stream << "    }";
        if (i + 1 < result.diagnostics.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ]\n";
}
} // namespace

std::string serializeManifest(const PipelineResult& result, const std::string& inputPath,
                              const std::string& outputDir, const std::string& timestamp,
                              const std::string& pipelineVersion)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"pipelineVersion\": \"" << escapeJson(pipelineVersion) << "\",\n";
    stream << "  \"timestamp\": \"" << escapeJson(timestamp) << "\",\n";
    stream << "  \"input\": {\n";
    stream << "    \"path\": \"" << escapeJson(summarizePath(inputPath)) << "\"\n";
    stream << "  },\n";
    stream << "  \"selection\": {\n";
    stream << "    \"rule\": \"" << escapeJson(result.selectionInfo.rule) << "\",\n";
    stream << "    \"reason\": \"" << escapeJson(result.selectionInfo.reason) << "\",\n";
    stream << "    \"selectedPath\": \""
           << escapeJson(summarizePath(result.selectionInfo.selectedPath)) << "\"\n";
    stream << "  },\n";
    stream << "  \"output\": {\n";
    stream << "    \"directory\": \"" << escapeJson(outputDir) << "\",\n";
    stream << "    \"module\": \"" << escapeJson(result.artifacts.moduleName) << "\",\n";
    stream << "    \"artifacts\": {\n";
    stream << "      \"header\": \"" << escapeJson(result.artifacts.headerPath) << "\",\n";
    stream << "      \"source\": \"" << escapeJson(result.artifacts.sourcePath) << "\",\n";
    stream << "      \"build\": \"" << escapeJson(result.artifacts.buildPath) << "\",\n";
    stream << "      \"manifest\": \"" << escapeJson(result.artifacts.manifestPath) << "\"\n";
    stream << "    },\n";
    stream << "    \"resources\": [\n";
    for (size_t i = 0; i < result.artifacts.exportedResources.size(); ++i)
    {
        stream << "      \"" << escapeJson(result.artifacts.exportedResources[i]) << "\"";
        if (i + 1 < result.artifacts.exportedResources.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "    ],\n";
    stream << "    \"runtimeInclude\": \"" << escapeJson(result.artifacts.runtimeIncludePath)
           << "\",\n";
    stream << "    \"runtimeSource\": \"" << escapeJson(result.artifacts.runtimeSourcePath)
           << "\"\n";
    stream << "  },\n";
    stream << "  \"discSet\": {\n";
    stream << "    \"setName\": \"" << escapeJson(result.discSet.setName) << "\",\n";
    stream << "    \"activeDiscIndex\": " << result.discSet.activeDiscIndex << ",\n";
    stream << "    \"discs\": [\n";
    for (size_t i = 0; i < result.discSet.discs.size(); ++i)
    {
        const auto& disc = result.discSet.discs[i];
        stream << "      {\n";
        stream << "        \"index\": " << disc.discIndex << ",\n";
        stream << "        \"path\": \"" << escapeJson(summarizePath(disc.path)) << "\",\n";
        stream << "        \"volumeLabel\": \"" << escapeJson(disc.volumeLabel) << "\"\n";
        stream << "      }";
        if (i + 1 < result.discSet.discs.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "    ]\n";
    stream << "  },\n";
    stream << "  \"exeCandidates\": [\n";
    for (size_t i = 0; i < result.exeCandidates.size(); ++i)
    {
        const auto& candidate = result.exeCandidates[i];
        stream << "    {\n";
        stream << "      \"path\": \"" << escapeJson(summarizePath(candidate.path)) << "\",\n";
        stream << "      \"loadAddress\": \"0x" << formatHex(candidate.loadAddress, 8) << "\",\n";
        stream << "      \"loadSize\": " << candidate.loadSize << ",\n";
        stream << "      \"entryPoint\": \"0x" << formatHex(candidate.entryPoint, 8) << "\",\n";
        stream << "      \"hash\": \"" << escapeJson(candidate.hash) << "\",\n";
        stream << "      \"valid\": " << (candidate.valid ? "true" : "false") << "\n";
        stream << "    }";
        if (i + 1 < result.exeCandidates.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ],\n";
    appendManifestFunctions(stream, result);
    appendManifestDiagnostics(stream, result);
    stream << "}\n";
    return stream.str();
}

bool writeOutputArtifacts(PipelineResult& result, const std::filesystem::path& outputDir,
                          const std::string& moduleName, const std::string& header,
                          const std::string& source, const std::string& runnerSource,
                          const std::string& buildFile, const std::string& activeDiscPath,
                          const std::filesystem::path& inputFsPath,
                          std::vector<std::string>& warnings,
                          std::vector<PipelineDiagnostic>& diagnostics,
                          const std::string& manifestTimestamp, const std::string& pipelineVersion,
                          std::string& outError)
{
    std::error_code dirError;
    std::filesystem::create_directories(outputDir, dirError);
    if (dirError)
    {
        outError = "Failed to create output directory: " + outputDir.string();
        return false;
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

    if (!writeFile(artifacts.headerPath, header, outError) ||
        !writeFile(artifacts.sourcePath, source, outError) ||
        !writeFile(runnerPath, runnerSource, outError) ||
        !writeFile(artifacts.buildPath, buildFile, outError))
    {
        return false;
    }

    std::filesystem::path repoRoot = repositoryRootFromSourcePath(std::filesystem::path(__FILE__));
    if (!copyDirectoryRecursive(repoRoot / "include" / "psxrecomp",
                                std::filesystem::path(artifacts.runtimeIncludePath) / "psxrecomp",
                                outError) ||
        !copyDirectoryRecursive(repoRoot / "src" / "runtime", artifacts.runtimeSourcePath,
                                outError))
    {
        return false;
    }

    if (isIsoLikePath(inputFsPath))
    {
        iso::IsoParser parser(activeDiscPath);
        if (parser.open() && parser.isValid())
        {
            std::filesystem::create_directories(artifacts.resourcesPath, dirError);
            if (dirError)
            {
                outError = "Failed to create resources directory: " + artifacts.resourcesPath;
                return false;
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
            for (const auto& error : parser.getErrors())
            {
                PipelineDiagnostic entry;
                entry.code = "IsoParserError";
                entry.severity = "error";
                entry.message = "Resource export skipped: " + error;
                entry.context.file = activeDiscPath;
                diagnostics.push_back(entry);
            }
            warnings.push_back("Resource export skipped due to parser errors.");
        }
    }

    result.success = true;
    result.artifacts = artifacts;
    result.warnings = warnings;
    result.diagnostics = diagnostics;

    const std::string timestamp = buildTimestamp(manifestTimestamp);
    const std::string manifest =
        serializeManifest(result, activeDiscPath, outputDir.string(), timestamp, pipelineVersion);
    if (!writeFile(artifacts.manifestPath, manifest, outError))
    {
        return false;
    }
    return true;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
