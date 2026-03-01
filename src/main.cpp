#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "psxrecomp/recompiler/pipeline.h"

// Placeholder for command-line interface
// This will be implemented as the project develops

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
        std::cout << "    \"resourceRoot\": \"" << escapeJson(result.artifacts.resourceRootPath)
                  << "\",\n";
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

void printUsage(const char* programName)
{
    std::cout << "PSXRecomp - PlayStation Static Recompiler\n";
    std::cout << "Version 0.1.0 (Early Development)\n\n";
    std::cout << "Usage: " << programName << " [options] <input.bin/.iso>\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o, --output <dir>     Output directory for recompiled code\n";
    std::cout << "  -O, --optimize         Enable optimizations\n";
    std::cout << "  -s, --symbols          Preserve debug symbols\n";
    std::cout << "  -v, --verbose          Verbose output\n";
    std::cout << "  --json                 Emit structured JSON output\n";
    std::cout << "  -h, --help             Show this help message\n\n";
    std::cout << "Example:\n";
    std::cout << "  " << programName << " -o output_dir game.bin\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    std::string inputFile;
    std::string outputDir = "./output";
    bool optimize = false;
    bool preserveSymbols = false;
    bool verbose = false;
    bool jsonOutput = false;

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-o" || arg == "--output")
        {
            if (i + 1 < argc)
            {
                outputDir = argv[++i];
            }
            else
            {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return 1;
            }
        }
        else if (arg == "-O" || arg == "--optimize")
        {
            optimize = true;
        }
        else if (arg == "-s" || arg == "--symbols")
        {
            preserveSymbols = true;
        }
        else if (arg == "-v" || arg == "--verbose")
        {
            verbose = true;
        }
        else if (arg == "--json")
        {
            jsonOutput = true;
        }
        else if (arg[0] != '-')
        {
            inputFile = arg;
        }
        else
        {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (inputFile.empty())
    {
        if (jsonOutput)
        {
            psxrecomp::recompiler::PipelineResult result;
            result.success = false;
            result.errorMessage = "No input file specified.";
            printJsonOutput(result, inputFile, outputDir);
        }
        else
        {
            std::cerr << "Error: No input file specified\n";
            printUsage(argv[0]);
        }
        return 1;
    }

    if (!jsonOutput)
    {
        std::cout << "PSXRecomp - Static Recompiler for PlayStation\n";
        std::cout << "=============================================\n\n";
        std::cout << "Input file:    " << inputFile << "\n";
        std::cout << "Output dir:    " << outputDir << "\n";
        std::cout << "Optimize:      " << (optimize ? "Yes" : "No") << "\n";
        std::cout << "Symbols:       " << (preserveSymbols ? "Preserve" : "Strip") << "\n";
        std::cout << "Verbose:       " << (verbose ? "Yes" : "No") << "\n\n";
    }

    psxrecomp::recompiler::PipelineOptions options;
    options.outputDirectory = outputDir;
    options.enableOptimizations = optimize;
    options.preserveSymbols = preserveSymbols;
    options.verbose = verbose;

    psxrecomp::recompiler::RecompilationPipeline pipeline(options);
    auto result = pipeline.run(inputFile);
    if (!result.success)
    {
        if (jsonOutput)
        {
            printJsonOutput(result, inputFile, outputDir);
        }
        else
        {
            std::cerr << "Recompilation failed:\n" << result.errorMessage << "\n";
        }
        return 1;
    }

    if (!jsonOutput && !result.warnings.empty())
    {
        std::cout << "Warnings:\n";
        for (const auto& warning : result.warnings)
        {
            std::cout << " - " << warning << "\n";
        }
        std::cout << "\n";
    }

    if (jsonOutput)
    {
        printJsonOutput(result, inputFile, outputDir);
        return 0;
    }

    std::cout << "Recompilation complete.\n";
    std::cout << "Module:   " << result.artifacts.moduleName << "\n";
    std::cout << "Header:   " << result.artifacts.headerPath << "\n";
    std::cout << "Source:   " << result.artifacts.sourcePath << "\n";
    std::cout << "CMake:    " << result.artifacts.buildPath << "\n";
    std::cout << "Manifest: " << result.artifacts.manifestPath << "\n";
    std::cout << "Resources:" << (result.artifacts.resourcesPath.empty() ? " (none)" : "")
              << (result.artifacts.resourcesPath.empty() ? ""
                                                         : " " + result.artifacts.resourcesPath)
              << "\n";
    std::cout << "Runtime include: " << result.artifacts.runtimeIncludePath << "\n";
    std::cout << "Runtime source:  " << result.artifacts.runtimeSourcePath << "\n";

    return 0;
}
