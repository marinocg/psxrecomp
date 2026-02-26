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
    if (!std::filesystem::is_directory(source, error) || error)
    {
        outError = "Source is not a directory: " + source.string();
        return false;
    }

    std::filesystem::create_directories(destination, error);
    if (error)
    {
        outError = "Failed to create destination directory: " + destination.string();
        return false;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(source))
    {
        error.clear();
        const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), source);
        const std::filesystem::path targetPath = destination / relativePath;

        if (entry.is_directory(error))
        {
            std::filesystem::create_directories(targetPath, error);
            if (error)
            {
                outError = "Failed to create destination directory: " + targetPath.string();
                return false;
            }
            continue;
        }

        std::filesystem::create_directories(targetPath.parent_path(), error);
        if (error)
        {
            outError =
                "Failed to create destination directory: " + targetPath.parent_path().string();
            return false;
        }

        if (entry.is_regular_file(error) || entry.is_symlink(error))
        {
            std::ifstream input(entry.path(), std::ios::binary);
            if (!input)
            {
                outError = "Failed to open source file: " + entry.path().string();
                return false;
            }

            std::ofstream output(targetPath, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                outError = "Failed to open destination file: " + targetPath.string();
                return false;
            }

            output << input.rdbuf();
            if (!output.good())
            {
                outError = "Failed to copy file from " + entry.path().string() + " to " +
                           targetPath.string();
                return false;
            }
            continue;
        }
    }
    return true;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
