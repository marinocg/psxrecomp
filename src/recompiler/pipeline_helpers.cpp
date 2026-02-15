#include "pipeline_helpers.h"

#include "psxrecomp/iso/iso_parser.h"
#include "psxrecomp/iso/multi_disc_set.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
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

std::string formatHex(u64 value, size_t width)
{
    std::ostringstream stream;
    stream << std::hex << std::uppercase << std::setw(static_cast<int>(width)) << std::setfill('0')
           << value;
    return stream.str();
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

std::string toDiagnosticCode(iso::PsxExeErrorCode code)
{
    switch (code)
    {
    case iso::PsxExeErrorCode::BufferTooSmall:
        return "BufferTooSmall";
    case iso::PsxExeErrorCode::FileOpenFailed:
        return "FileOpenFailed";
    case iso::PsxExeErrorCode::FileEmpty:
        return "FileEmpty";
    case iso::PsxExeErrorCode::FileReadFailed:
        return "FileReadFailed";
    case iso::PsxExeErrorCode::InvalidMagic:
        return "InvalidMagic";
    case iso::PsxExeErrorCode::PayloadTooSmall:
        return "PayloadTooSmall";
    case iso::PsxExeErrorCode::LoadAddressOutOfRange:
        return "LoadAddressOutOfRange";
    case iso::PsxExeErrorCode::LoadAddressMisaligned:
        return "LoadAddressMisaligned";
    case iso::PsxExeErrorCode::LoadSizeMisaligned:
        return "LoadSizeMisaligned";
    case iso::PsxExeErrorCode::LoadSizeMismatch:
        return "LoadSizeMismatch";
    case iso::PsxExeErrorCode::InitialPcOutOfRange:
        return "InitialPcOutOfRange";
    case iso::PsxExeErrorCode::InitialPcMisaligned:
        return "InitialPcMisaligned";
    case iso::PsxExeErrorCode::InitialGpOutOfRange:
        return "InitialGpOutOfRange";
    case iso::PsxExeErrorCode::InitialGpMisaligned:
        return "InitialGpMisaligned";
    case iso::PsxExeErrorCode::BssOutOfRange:
        return "BssOutOfRange";
    case iso::PsxExeErrorCode::BssMisaligned:
        return "BssMisaligned";
    case iso::PsxExeErrorCode::BssSizeMisaligned:
        return "BssSizeMisaligned";
    case iso::PsxExeErrorCode::StackOutOfRange:
        return "StackOutOfRange";
    case iso::PsxExeErrorCode::StackMisaligned:
        return "StackMisaligned";
    case iso::PsxExeErrorCode::StackSizeMisaligned:
        return "StackSizeMisaligned";
    case iso::PsxExeErrorCode::OverlayTableMalformed:
        return "OverlayTableMalformed";
    case iso::PsxExeErrorCode::OverlayEntryOutOfRange:
        return "OverlayEntryOutOfRange";
    case iso::PsxExeErrorCode::OverlayEntryMisaligned:
        return "OverlayEntryMisaligned";
    }
    return "UnknownError";
}

PipelineDiagnostic toPipelineDiagnostic(const iso::PsxExeDiagnostic& diagnostic,
                                        const std::string& sourcePath)
{
    PipelineDiagnostic entry;
    entry.code = toDiagnosticCode(diagnostic.code);
    entry.severity =
        diagnostic.severity == iso::PsxExeDiagnosticSeverity::Error ? "error" : "warning";
    entry.message = diagnostic.field + ": " + diagnostic.message;
    entry.context.file = sourcePath;
    entry.context.offset = std::nullopt;
    return entry;
}

void appendDiagnostics(std::vector<PipelineDiagnostic>& diagnostics,
                       std::vector<std::string>& warnings,
                       const iso::PsxExeDiagnostics& exeDiagnostics, const std::string& sourcePath)
{
    for (const auto& entry : exeDiagnostics.entries)
    {
        diagnostics.push_back(toPipelineDiagnostic(entry, sourcePath));
        std::ostringstream stream;
        stream << (entry.severity == iso::PsxExeDiagnosticSeverity::Error ? "error: " : "warning: ")
               << entry.field << " - " << entry.message;
        warnings.push_back(stream.str());
    }
}

u64 fnv1a64(const std::vector<u8>& data)
{
    constexpr u64 kOffset = 1469598103934665603ULL;
    constexpr u64 kPrime = 1099511628211ULL;
    u64 hash = kOffset;
    for (u8 byte : data)
    {
        hash ^= static_cast<u64>(byte);
        hash *= kPrime;
    }
    return hash;
}

std::string buildTimestamp(const std::string& overrideTimestamp)
{
    if (!overrideTimestamp.empty())
    {
        return overrideTimestamp;
    }
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm utcTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &nowTime);
#else
    gmtime_r(&nowTime, &utcTime);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

DiscSetMetadata buildDiscSetMetadata(const std::vector<std::string>& discPaths, size_t activeIndex,
                                     std::vector<std::string>& warnings)
{
    DiscSetMetadata metadata;
    if (discPaths.empty())
    {
        return metadata;
    }

    metadata.activeDiscIndex = static_cast<u32>(activeIndex);
    if (discPaths.size() > 1)
    {
        iso::MultiDiscSet discSet(discPaths);
        if (!discSet.open())
        {
            warnings.push_back("Failed to open one or more discs for multi-disc set.");
        }
        for (size_t i = 0; i < discSet.getDiscCount(); ++i)
        {
            const auto& info = discSet.getDiscInfo(i);
            DiscMetadata disc;
            disc.path = info.path;
            disc.volumeLabel = info.volumeLabel;
            disc.discIndex = static_cast<u32>(i);
            metadata.discs.push_back(std::move(disc));
        }
    }
    else
    {
        DiscMetadata disc;
        disc.path = discPaths.front();
        if (isIsoLikePath(std::filesystem::path(disc.path)))
        {
            iso::IsoParser parser(disc.path);
            if (parser.open() && parser.isValid())
            {
                disc.volumeLabel = parser.getVolumeLabel();
            }
        }
        disc.discIndex = 0;
        metadata.discs.push_back(std::move(disc));
    }

    for (const auto& disc : metadata.discs)
    {
        if (!disc.volumeLabel.empty())
        {
            metadata.setName = disc.volumeLabel;
            break;
        }
    }
    if (metadata.setName.empty())
    {
        std::filesystem::path firstPath(metadata.discs.front().path);
        metadata.setName = firstPath.stem().string();
    }
    return metadata;
}

std::string makeDeterministicTag(u32 loadAddress, const std::string& hash)
{
    std::ostringstream stream;
    stream << "0x" << formatHex(loadAddress, 8);
    if (!hash.empty())
    {
        stream << "_" << hash.substr(0, std::min<size_t>(8, hash.size()));
    }
    return stream.str();
}

std::string buildOutputDirectory(const std::filesystem::path& baseDir, const std::string& inputStem,
                                 const std::string& volumeLabel, const std::string& exeTag)
{
    std::filesystem::path outputDir = baseDir;
    outputDir /= sanitizeModuleName(inputStem);
    outputDir /= sanitizeModuleName(volumeLabel.empty() ? "no_label" : volumeLabel);
    outputDir /= sanitizeModuleName(exeTag);
    return outputDir.string();
}

std::filesystem::path repositoryRootFromSourcePath(const std::filesystem::path& sourcePath)
{
    std::filesystem::path root = sourcePath;
    for (int i = 0; i < 3; ++i)
    {
        if (!root.has_parent_path())
        {
            break;
        }
        root = root.parent_path();
    }
    return root;
}

bool copyDirectoryRecursive(const std::filesystem::path& source,
                            const std::filesystem::path& destination, std::string& outError)
{
    std::error_code error;
    if (!std::filesystem::exists(source, error) || error)
    {
        outError = "Missing source directory: " + source.string();
        return false;
    }
    std::filesystem::create_directories(destination, error);
    if (error)
    {
        outError = "Failed to create destination directory: " + destination.string();
        return false;
    }
    std::filesystem::copy(source, destination,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          error);
    if (error)
    {
        outError =
            "Failed to copy directory from " + source.string() + " to " + destination.string();
        return false;
    }
    return true;
}

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
    stream << "}\n";
    return stream.str();
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
