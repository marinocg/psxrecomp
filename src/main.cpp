#include <cstdlib>
#include <iostream>
#include <string>

#include "psxrecomp/recompiler/pipeline.h"

// Placeholder for command-line interface
// This will be implemented as the project develops

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
        std::cerr << "Error: No input file specified\n";
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "PSXRecomp - Static Recompiler for PlayStation\n";
    std::cout << "=============================================\n\n";
    std::cout << "Input file:    " << inputFile << "\n";
    std::cout << "Output dir:    " << outputDir << "\n";
    std::cout << "Optimize:      " << (optimize ? "Yes" : "No") << "\n";
    std::cout << "Symbols:       " << (preserveSymbols ? "Preserve" : "Strip") << "\n";
    std::cout << "Verbose:       " << (verbose ? "Yes" : "No") << "\n\n";

    psxrecomp::recompiler::PipelineOptions options;
    options.outputDirectory = outputDir;
    options.enableOptimizations = optimize;
    options.preserveSymbols = preserveSymbols;
    options.verbose = verbose;

    psxrecomp::recompiler::RecompilationPipeline pipeline(options);
    auto result = pipeline.run(inputFile);
    if (!result.success)
    {
        std::cerr << "Recompilation failed:\n" << result.errorMessage << "\n";
        return 1;
    }

    if (!result.warnings.empty())
    {
        std::cout << "Warnings:\n";
        for (const auto& warning : result.warnings)
        {
            std::cout << " - " << warning << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "Recompilation complete.\n";
    std::cout << "Module:   " << result.artifacts.moduleName << "\n";
    std::cout << "Header:   " << result.artifacts.headerPath << "\n";
    std::cout << "Source:   " << result.artifacts.sourcePath << "\n";
    std::cout << "CMake:    " << result.artifacts.buildPath << "\n";

    return 0;
}
