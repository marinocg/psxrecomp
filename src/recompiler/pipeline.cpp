#include "psxrecomp/recompiler/pipeline.h"

#include "psxrecomp/disasm/instruction.h"
#include "psxrecomp/ir/control_flow.h"
#include "psxrecomp/ir/mips_ir_builder.h"
#include "psxrecomp/iso/iso_parser.h"
#include "psxrecomp/iso/psx_exe_loader.h"
#include "psxrecomp/recompiler/codegen.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace psxrecomp
{
namespace recompiler
{

namespace
{
std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool isIsoLikePath(const std::filesystem::path& path)
{
    const std::string extension = toLower(path.extension().string());
    return extension == ".iso" || extension == ".bin" || extension == ".cue";
}

std::string sanitizeModuleName(const std::string& name)
{
    std::string result;
    result.reserve(name.size());
    for (char ch : name)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')
        {
            result.push_back(ch);
        }
        else
        {
            result.push_back('_');
        }
    }
    if (result.empty())
    {
        result = "psx_module";
    }
    if (std::isdigit(static_cast<unsigned char>(result.front())))
    {
        result.insert(result.begin(), '_');
    }
    return result;
}

std::string formatDiagnostics(const iso::PsxExeDiagnostics& diagnostics)
{
    std::ostringstream stream;
    for (const auto& entry : diagnostics.entries)
    {
        stream << (entry.severity == iso::PsxExeDiagnosticSeverity::Error ? "error: " : "warning: ")
               << entry.field << " - " << entry.message << "\n";
    }
    return stream.str();
}

void appendDiagnosticsWarnings(std::vector<std::string>& warnings,
                               const iso::PsxExeDiagnostics& diagnostics)
{
    for (const auto& entry : diagnostics.entries)
    {
        std::ostringstream stream;
        stream << (entry.severity == iso::PsxExeDiagnosticSeverity::Error ? "error: " : "warning: ")
               << entry.field << " - " << entry.message;
        warnings.push_back(stream.str());
    }
}

bool writeFile(const std::filesystem::path& path, const std::string& contents,
               std::string& outError)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        outError = "Failed to open output file: " + path.string();
        return false;
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file)
    {
        outError = "Failed to write output file: " + path.string();
        return false;
    }
    return true;
}

Address resolveEntryAddress(const iso::PsxExeImage& image)
{
    if (image.entryPoint.pc != 0)
    {
        return image.entryPoint.pc;
    }
    return image.header.loadAddress;
}

PipelineResult buildPipelineError(const std::string& message,
                                  const std::vector<std::string>& warnings = {})
{
    PipelineResult result;
    result.success = false;
    result.errorMessage = message;
    result.warnings = warnings;
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
        return buildPipelineError("Input path is empty.");
    }

    if (m_options.outputDirectory.empty())
    {
        return buildPipelineError("Output directory is not set.");
    }

    const std::filesystem::path inputFsPath = std::filesystem::path(inputPath);

    iso::PsxExeImage exeImage{};
    std::vector<std::string> warnings;

    if (isIsoLikePath(inputFsPath))
    {
        iso::IsoParser parser(inputPath);
        if (!parser.open() || !parser.isValid())
        {
            std::string error = "Failed to open ISO image.";
            if (!parser.getLastError().empty())
            {
                error += " " + parser.getLastError();
            }
            return buildPipelineError(error);
        }

        std::string executablePath = parser.findExecutable();
        if (executablePath.empty())
        {
            auto candidates = parser.listExecutables();
            if (!candidates.empty())
            {
                executablePath = candidates.front();
                warnings.push_back("Using first executable candidate: " + executablePath);
            }
        }
        if (executablePath.empty())
        {
            return buildPipelineError("No PSX executable found in ISO image.", warnings);
        }

        std::vector<u8> exeData = parser.extractFile(executablePath);
        if (exeData.empty())
        {
            return buildPipelineError("Failed to extract PSX executable from ISO.", warnings);
        }

        iso::PsxExeDiagnostics diagnostics;
        if (!iso::PsxExeLoader::loadImage(exeData, exeImage, &diagnostics))
        {
            return buildPipelineError(
                "Failed to parse PSX executable:\n" + formatDiagnostics(diagnostics), warnings);
        }
        appendDiagnosticsWarnings(warnings, diagnostics);
    }
    else
    {
        iso::PsxExeDiagnostics diagnostics;
        if (!iso::PsxExeLoader::loadFromFile(inputPath, exeImage, &diagnostics))
        {
            return buildPipelineError(
                "Failed to load PSX executable:\n" + formatDiagnostics(diagnostics), warnings);
        }
        appendDiagnosticsWarnings(warnings, diagnostics);
    }

    if (exeImage.programData.empty())
    {
        return buildPipelineError("Executable program data is empty.", warnings);
    }

    const Address baseAddress = exeImage.header.loadAddress;
    const auto disassembled = disasm::MipsDisassembler::disassemble(
        exeImage.programData.data(), exeImage.programData.size(), baseAddress);

    auto irBuild = ir::buildIrFromMips(disassembled);
    warnings.insert(warnings.end(), irBuild.warnings.begin(), irBuild.warnings.end());

    if (!irBuild.errors.empty())
    {
        std::ostringstream stream;
        stream << "IR build failed:\n";
        for (const auto& error : irBuild.errors)
        {
            stream << " - " << error << "\n";
        }
        return buildPipelineError(stream.str(), warnings);
    }

    const Address entryAddress = resolveEntryAddress(exeImage);
    auto flowResult = ir::buildControlFlowFunction("entry", entryAddress, irBuild.instructions);
    if (!flowResult.errors.empty())
    {
        std::ostringstream stream;
        stream << "Control-flow build failed:\n";
        for (const auto& error : flowResult.errors)
        {
            stream << " - " << error << "\n";
        }
        return buildPipelineError(stream.str(), warnings);
    }

    ir::Program program;
    program.functions.push_back(std::move(flowResult.function));

    CodeGenOptions codegenOptions;
    codegenOptions.enableOptimizations = m_options.enableOptimizations;
    codegenOptions.preserveSymbols = m_options.preserveSymbols;
    CodeGenerator codeGenerator(codegenOptions);

    const std::string moduleName = sanitizeModuleName(
        inputFsPath.stem().string().empty() ? "psx_module" : inputFsPath.stem().string());

    const std::string header = codeGenerator.generateHeader(program, moduleName);
    const std::string source = codeGenerator.generateSource(program, moduleName);
    const std::string buildFile = codeGenerator.generateBuildFile(moduleName);

    std::filesystem::path outputDir = std::filesystem::path(m_options.outputDirectory);
    std::error_code dirError;
    std::filesystem::create_directories(outputDir, dirError);
    if (dirError)
    {
        return buildPipelineError("Failed to create output directory: " + outputDir.string(),
                                  warnings);
    }

    PipelineArtifacts artifacts;
    artifacts.moduleName = moduleName;
    artifacts.headerPath = (outputDir / (moduleName + ".h")).string();
    artifacts.sourcePath = (outputDir / (moduleName + ".cpp")).string();
    artifacts.buildPath = (outputDir / "CMakeLists.txt").string();

    std::string writeError;
    if (!writeFile(artifacts.headerPath, header, writeError) ||
        !writeFile(artifacts.sourcePath, source, writeError) ||
        !writeFile(artifacts.buildPath, buildFile, writeError))
    {
        return buildPipelineError(writeError, warnings);
    }

    PipelineResult result;
    result.success = true;
    result.artifacts = artifacts;
    result.warnings = warnings;
    return result;
}

} // namespace recompiler
} // namespace psxrecomp
