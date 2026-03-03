#include "main_json_output.h"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

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
        case '\"':
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

} // namespace

void printJsonOutput(const psxrecomp::recompiler::PipelineResult& result,
                     const std::string& inputFile, const std::string& outputDir)
{
    auto toHex = [](psxrecomp::u32 value)
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
        return stream.str();
    };

    std::cout << "{\n";
    std::cout << "  \"success\": " << (result.success ? "true" : "false") << ",\n";
    std::cout << "  \"input\": \"" << escapeJson(summarizePath(inputFile)) << "\",\n";
    std::cout << "  \"outputDir\": \"" << escapeJson(outputDir) << "\",\n";
    if (!result.errorMessage.empty())
    {
        std::cout << "  \"error\": \"" << escapeJson(result.errorMessage) << "\",\n";
    }
    std::cout << "  \"selection\": {\n";
    std::cout << "    \"rule\": \"" << escapeJson(result.selectionInfo.rule) << "\",\n";
    std::cout << "    \"reason\": \"" << escapeJson(result.selectionInfo.reason) << "\",\n";
    std::cout << "    \"selectedPath\": \""
              << escapeJson(summarizePath(result.selectionInfo.selectedPath)) << "\"\n";
    std::cout << "  },\n";
    if (result.success)
    {
        std::cout << "  \"artifacts\": {\n";
        std::cout << "    \"module\": \"" << escapeJson(result.artifacts.moduleName) << "\",\n";
        std::cout << "    \"header\": \"" << escapeJson(result.artifacts.headerPath) << "\",\n";
        std::cout << "    \"source\": \"" << escapeJson(result.artifacts.sourcePath) << "\",\n";
        std::cout << "    \"build\": \"" << escapeJson(result.artifacts.buildPath) << "\",\n";
        std::cout << "    \"manifest\": \"" << escapeJson(result.artifacts.manifestPath) << "\",\n";
        std::cout << "    \"resources\": \"" << escapeJson(result.artifacts.resourcesPath)
                  << "\",\n";
        if (!result.artifacts.resourceRootPath.empty() &&
            result.artifacts.resourceRootPath != result.artifacts.resourcesPath)
        {
            std::cout << "    \"resourceRoot\": \"" << escapeJson(result.artifacts.resourceRootPath)
                      << "\",\n";
        }
        std::cout << "    \"resourceManifest\": \""
                  << escapeJson(result.artifacts.resourceManifestPath) << "\",\n";
        std::cout << "    \"runtimeInclude\": \"" << escapeJson(result.artifacts.runtimeIncludePath)
                  << "\",\n";
        std::cout << "    \"runtimeSource\": \"" << escapeJson(result.artifacts.runtimeSourcePath)
                  << "\"\n";
        std::cout << "  },\n";
    }
    std::cout << "  \"warnings\": [\n";
    for (size_t index = 0; index < result.warnings.size(); ++index)
    {
        std::cout << "    \"" << escapeJson(result.warnings[index]) << "\"";
        if (index + 1 < result.warnings.size())
        {
            std::cout << ",";
        }
        std::cout << "\n";
    }
    std::cout << "  ],\n";
    std::cout << "  \"diagnostics\": [\n";
    for (size_t index = 0; index < result.diagnostics.size(); ++index)
    {
        const auto& diag = result.diagnostics[index];
        std::cout << "    {\n";
        std::cout << "      \"code\": \"" << escapeJson(diag.code) << "\",\n";
        std::cout << "      \"severity\": \"" << escapeJson(diag.severity) << "\",\n";
        std::cout << "      \"message\": \"" << escapeJson(diag.message) << "\",\n";
        std::cout << "      \"context\": {\n";
        std::cout << "        \"file\": \"" << escapeJson(summarizePath(diag.context.file))
                  << "\",\n";
        std::cout << "        \"module\": \"" << escapeJson(diag.context.module) << "\"";
        if (diag.context.offset.has_value())
        {
            std::cout << ",\n        \"offset\": " << diag.context.offset.value() << "\n";
        }
        else
        {
            std::cout << "\n";
        }
        std::cout << "      }\n";
        std::cout << "    }";
        if (index + 1 < result.diagnostics.size())
        {
            std::cout << ",";
        }
        std::cout << "\n";
    }
    std::cout << "  ],\n";
    std::cout << "  \"exeCandidates\": [\n";
    for (size_t index = 0; index < result.exeCandidates.size(); ++index)
    {
        const auto& candidate = result.exeCandidates[index];
        std::cout << "    {\n";
        std::cout << "      \"path\": \"" << escapeJson(summarizePath(candidate.path)) << "\",\n";
        std::cout << "      \"loadAddress\": \"" << toHex(candidate.loadAddress) << "\",\n";
        std::cout << "      \"loadSize\": " << candidate.loadSize << ",\n";
        std::cout << "      \"entryPoint\": \"" << toHex(candidate.entryPoint) << "\",\n";
        std::cout << "      \"hash\": \"" << escapeJson(candidate.hash) << "\",\n";
        std::cout << "      \"valid\": " << (candidate.valid ? "true" : "false") << "\n";
        std::cout << "    }";
        if (index + 1 < result.exeCandidates.size())
        {
            std::cout << ",";
        }
        std::cout << "\n";
    }
    std::cout << "  ]\n";
    std::cout << "}\n";
}
