#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "main_json_output.h"
#include "psxrecomp/recompiler/pipeline.h"

// Placeholder for command-line interface
// This will be implemented as the project develops

std::string toLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::optional<uint64_t> parseUint64Env(const char* name)
{
    const char* raw = std::getenv(name);
    if (!raw || raw[0] == '\0')
    {
        return std::nullopt;
    }
    if (raw[0] == '+' || raw[0] == '-')
    {
        return std::nullopt;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(raw, &end, 10);
    if (errno != 0 || end == raw || (end && *end != '\0'))
    {
        return std::nullopt;
    }
    return static_cast<uint64_t>(value);
}

std::optional<bool> parseBoolEnv(const char* name)
{
    const char* raw = std::getenv(name);
    if (!raw || raw[0] == '\0')
    {
        return std::nullopt;
    }
    const std::string value = toLowerCopy(raw);
    if (value == "1" || value == "true" || value == "yes" || value == "on")
    {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off")
    {
        return false;
    }
    return std::nullopt;
}

std::vector<std::string> parsePrefixListEnv(const char* name)
{
    std::vector<std::string> values;
    const char* raw = std::getenv(name);
    if (!raw || raw[0] == '\0')
    {
        return values;
    }

    std::string token;
    std::istringstream stream(raw);
    while (std::getline(stream, token, ','))
    {
        const auto start = token.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            continue;
        }
        const auto end = token.find_last_not_of(" \t\r\n");
        values.push_back(token.substr(start, end - start + 1));
    }
    return values;
}

void applyResourceExportEnv(psxrecomp::recompiler::PipelineOptions& options)
{
    auto& resourceOptions = options.resourceExport;
    if (const char* mode = std::getenv("PSXRECOMP_RES_FS_MODE"))
    {
        const std::string normalized = toLowerCopy(mode);
        if (normalized == "minimal")
        {
            resourceOptions.fsMode =
                psxrecomp::recompiler::PipelineOptions::ResourceExportOptions::FsMode::Minimal;
        }
        else if (normalized == "smart")
        {
            resourceOptions.fsMode =
                psxrecomp::recompiler::PipelineOptions::ResourceExportOptions::FsMode::Smart;
        }
        else if (normalized == "full")
        {
            resourceOptions.fsMode =
                psxrecomp::recompiler::PipelineOptions::ResourceExportOptions::FsMode::Full;
        }
    }

    if (const auto maxTotal = parseUint64Env("PSXRECOMP_RES_MAX_TOTAL_BYTES"))
    {
        resourceOptions.maxTotalBytes = *maxTotal;
    }
    if (const auto maxSingle = parseUint64Env("PSXRECOMP_RES_MAX_SINGLE_FILE_BYTES"))
    {
        resourceOptions.maxSingleFileBytes = *maxSingle;
    }
    if (const auto alwaysSystem = parseBoolEnv("PSXRECOMP_RES_ALWAYS_EXPORT_SYSTEM_CNF"))
    {
        resourceOptions.alwaysExportSystemCnf = *alwaysSystem;
    }
    if (const auto alwaysBoot = parseBoolEnv("PSXRECOMP_RES_ALWAYS_EXPORT_BOOT_EXE"))
    {
        resourceOptions.alwaysExportBootExe = *alwaysBoot;
    }
    if (const auto alwaysAllExe = parseBoolEnv("PSXRECOMP_RES_ALWAYS_EXPORT_ALL_EXE"))
    {
        resourceOptions.alwaysExportAllExe = *alwaysAllExe;
    }

    if (std::getenv("PSXRECOMP_RES_ALLOW_PREFIXES") != nullptr)
    {
        resourceOptions.allowPrefixes = parsePrefixListEnv("PSXRECOMP_RES_ALLOW_PREFIXES");
    }
    if (std::getenv("PSXRECOMP_RES_DENY_PREFIXES") != nullptr)
    {
        resourceOptions.denyPrefixes = parsePrefixListEnv("PSXRECOMP_RES_DENY_PREFIXES");
    }
    if (const auto embeddedScan = parseBoolEnv("PSXRECOMP_RES_EMBEDDED_SCAN"))
    {
        resourceOptions.enableEmbeddedScan = *embeddedScan;
    }
    if (const auto discBlobEnabled = parseBoolEnv("PSXRECOMP_RES_DISC_BLOB_ENABLED"))
    {
        resourceOptions.discBlob.enabled = *discBlobEnabled;
    }
    if (const char* mode = std::getenv("PSXRECOMP_RES_DISC_BLOB_MODE"))
    {
        const std::string normalized = toLowerCopy(mode);
        if (normalized == "auto")
        {
            resourceOptions.discBlob.mode =
                psxrecomp::recompiler::PipelineOptions::ResourceExportOptions::DiscBlobMode::Auto;
        }
        else if (normalized == "force2048")
        {
            resourceOptions.discBlob.mode = psxrecomp::recompiler::PipelineOptions::
                ResourceExportOptions::DiscBlobMode::Force2048;
        }
        else if (normalized == "force2352")
        {
            resourceOptions.discBlob.mode = psxrecomp::recompiler::PipelineOptions::
                ResourceExportOptions::DiscBlobMode::Force2352;
        }
        else if (normalized == "disabled")
        {
            resourceOptions.discBlob.mode = psxrecomp::recompiler::PipelineOptions::
                ResourceExportOptions::DiscBlobMode::Disabled;
            resourceOptions.discBlob.enabled = false;
        }
    }
    if (const auto discBlobMaxBytes = parseUint64Env("PSXRECOMP_RES_DISC_BLOB_MAX_BYTES"))
    {
        resourceOptions.discBlob.maxBytes = *discBlobMaxBytes;
    }

    if (const auto exportDataTrackBlob = parseBoolEnv("PSXRECOMP_RES_EXPORT_DATA_TRACK_BLOB"))
    {
        resourceOptions.discBlob.enabled = *exportDataTrackBlob;
    }
    if (const auto maxBlobBytes = parseUint64Env("PSXRECOMP_RES_MAX_BLOB_BYTES"))
    {
        resourceOptions.discBlob.maxBytes = *maxBlobBytes;
    }
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
    applyResourceExportEnv(options);

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
