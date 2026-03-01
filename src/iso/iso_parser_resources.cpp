#include "psxrecomp/iso/iso_parser.h"

#include "iso_sector.h"
#include "iso_utils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace psxrecomp
{
namespace iso
{

namespace
{

std::vector<std::string> extensionsForType(ResourceType type)
{
    switch (type)
    {
    case ResourceType::TimTexture:
        return {".TIM"};
    case ResourceType::StrVideo:
        return {".STR"};
    case ResourceType::XaAudio:
        return {".XA"};
    default:
        return {};
    }
}

std::string normalizeExtension(std::string value)
{
    value = detail::toUpper(value);
    if (!value.empty() && value[0] != '.')
    {
        value.insert(value.begin(), '.');
    }
    return value;
}

} // namespace

std::vector<std::string> IsoParser::listResources(ResourceType type)
{
    return listFilesByExtension(extensionsForType(type), type == ResourceType::XaAudio);
}

bool IsoParser::exportResources(ResourceType type, const std::string& outputDirectory)
{
    if (!m_isOpen)
    {
        return false;
    }

    auto resources = listResources(type);
    if (resources.empty())
    {
        return true;
    }

    std::filesystem::path outputPath(outputDirectory);
    std::error_code error;
    std::filesystem::create_directories(outputPath, error);
    if (error)
    {
        addError("Failed to create output directory: " + outputPath.string());
        return false;
    }

    bool success = true;
    for (const auto& resource : resources)
    {
        auto data = extractFile(resource);
        if (data.empty())
        {
            success = false;
            continue;
        }
        std::filesystem::path resourcePath(resource);
        if (resourcePath.empty())
        {
            continue;
        }
        std::filesystem::path destination = outputPath / resourcePath;
        error.clear();
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
        {
            addError("Failed to create output directory for resource: " +
                     destination.parent_path().string());
            success = false;
            continue;
        }
        std::ofstream out(destination, std::ios::binary);
        if (!out)
        {
            addError("Failed to write resource file: " + destination.string());
            success = false;
            continue;
        }
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        if (!out.good())
        {
            addError("Failed to flush resource file: " + destination.string());
            success = false;
        }
    }
    return success;
}

std::vector<std::string> IsoParser::listFilesByExtension(const std::vector<std::string>& extensions,
                                                         bool requireXaAudio)
{
    std::vector<std::string> matches;

    std::vector<std::string> normalizedExtensions;
    normalizedExtensions.reserve(extensions.size());
    for (const auto& extension : extensions)
    {
        normalizedExtensions.push_back(normalizeExtension(extension));
    }

    const auto entries = listAllFilesRecursive();
    for (const auto& entry : entries)
    {
        if (entry.isDirectory)
        {
            continue;
        }

        auto dot = entry.path.find_last_of('.');
        if (dot == std::string::npos)
        {
            continue;
        }
        std::string extension = normalizeExtension(entry.path.substr(dot));
        if (std::find(normalizedExtensions.begin(), normalizedExtensions.end(), extension) ==
            normalizedExtensions.end())
        {
            continue;
        }
        if (requireXaAudio && !validateXaAudioFile(entry.path))
        {
            continue;
        }
        matches.push_back(entry.path);
    }

    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    return matches;
}

bool IsoParser::validateXaAudioFile(const std::string& path)
{
    DirectoryRecord target{};
    std::vector<DirectoryRecord> extents;
    if (!findFileExtents(path, target, extents))
    {
        return false;
    }

    if (m_rawSectorSize != detail::kRawSectorSize)
    {
        // For pure 2048-byte ISO images there is no XA subheader/EDC metadata,
        // so strict XA validation is impossible. Accept non-empty .XA resources
        // based on filesystem extents so resources can still be exported.
        if (target.dataLength == 0 || extents.empty())
        {
            addError("XA resource entry has no data extents.");
            return false;
        }
        return true;
    }

    std::sort(extents.begin(), extents.end(),
              [](const DirectoryRecord& lhs, const DirectoryRecord& rhs)
              { return lhs.extentLocation < rhs.extentLocation; });

    std::vector<u8> raw;
    u32 audioSectorCount = 0;
    for (const auto& extent : extents)
    {
        u32 remaining = extent.dataLength;
        u32 sector = extent.extentLocation;
        while (remaining > 0)
        {
            if (!readRawSector(sector, raw))
            {
                addError("Failed to read XA sector for validation.");
                return false;
            }
            detail::XaSubheader subheader{};
            if (!detail::isXaAudioSector(raw, &subheader))
            {
                addError("Non-XA audio sector encountered.");
                return false;
            }
            ++audioSectorCount;
            auto view = detail::decodeSectorLayout(raw);
            if (view.size != 2324)
            {
                addError("Unsupported XA sector layout.");
                return false;
            }
            u32 toConsume = std::min<u32>(remaining, static_cast<u32>(view.size));
            remaining -= toConsume;
            ++sector;
        }
    }

    if (audioSectorCount == 0)
    {
        addError("No XA audio sectors detected.");
        return false;
    }
    return true;
}

} // namespace iso
} // namespace psxrecomp
