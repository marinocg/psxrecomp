#include "psxrecomp/iso/iso_parser.h"

#include "iso_sector.h"
#include "iso_utils.h"
#include "psxrecomp/iso/iso_boot.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace iso
{

namespace
{

constexpr u32 kUserDataSize = detail::kUserDataSize;

bool isIsoRelativePath(const std::string& path)
{
    if (path.empty())
    {
        return false;
    }

    std::filesystem::path fsPath(path);
    if (fsPath.is_absolute())
    {
        return false;
    }

    const std::vector<std::string> components = detail::splitPath(path);
    if (components.empty())
    {
        return false;
    }
    for (const auto& component : components)
    {
        if (component.empty() || component == "." || component == "..")
        {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<u8> IsoParser::extractFile(const std::string& path)
{
    if (!m_isOpen)
    {
        return {};
    }

    DirectoryRecord target{};
    std::vector<DirectoryRecord> targetExtents;
    if (!findFileExtents(path, target, targetExtents))
    {
        return {};
    }

    std::vector<u8> data;
    std::sort(targetExtents.begin(), targetExtents.end(),
              [](const DirectoryRecord& lhs, const DirectoryRecord& rhs)
              { return lhs.extentLocation < rhs.extentLocation; });

    u32 totalLength = 0;
    if (targetExtents.size() == 1)
    {
        totalLength = targetExtents.front().dataLength;
    }
    else
    {
        bool hasMultiExtent = false;
        for (const auto& extent : targetExtents)
        {
            hasMultiExtent = hasMultiExtent || ((extent.flags & 0x80) != 0);
            totalLength += extent.dataLength;
        }
        if (!hasMultiExtent && !targetExtents.empty())
        {
            totalLength = targetExtents.front().dataLength;
            targetExtents = {targetExtents.front()};
        }
    }
    data.reserve(totalLength);

    for (const auto& extent : targetExtents)
    {
        u32 remaining = extent.dataLength;
        u32 sector = extent.extentLocation;
        while (remaining > 0)
        {
            auto sectorData = readSector(sector);
            if (sectorData.empty())
            {
                return {};
            }
            u32 toCopy = std::min<u32>(remaining, static_cast<u32>(sectorData.size()));
            data.insert(data.end(), sectorData.begin(), sectorData.begin() + toCopy);
            remaining -= toCopy;
            ++sector;
        }
    }

    return data;
}

bool IsoParser::exportFileTo(const std::string& isoPath, const std::filesystem::path& destination,
                             std::string* outError)
{
    std::ofstream out;
    bool wroteTempFile = false;
    std::filesystem::path tempDestination;
    auto cleanupTemp = [&]()
    {
        if (out.is_open())
        {
            out.close();
        }
        if (wroteTempFile)
        {
            std::error_code cleanupError;
            std::filesystem::remove(tempDestination, cleanupError);
        }
    };

    auto setError = [&](const std::string& message)
    {
        cleanupTemp();
        addError(message);
        if (outError)
        {
            *outError = message;
        }
        return false;
    };

    if (!m_isOpen)
    {
        return setError("ISO parser is not open.");
    }

    if (!isIsoRelativePath(isoPath))
    {
        return setError("ISO path must be relative and must not contain traversal segments.");
    }

    if (destination.empty())
    {
        return setError("Destination path is empty.");
    }

    DirectoryRecord target{};
    std::vector<DirectoryRecord> targetExtents;
    if (!findFileExtents(isoPath, target, targetExtents))
    {
        return setError("Failed to locate ISO file: " + isoPath);
    }

    std::sort(targetExtents.begin(), targetExtents.end(),
              [](const DirectoryRecord& lhs, const DirectoryRecord& rhs)
              { return lhs.extentLocation < rhs.extentLocation; });

    u32 totalLength = 0;
    if (targetExtents.size() == 1)
    {
        totalLength = targetExtents.front().dataLength;
    }
    else
    {
        bool hasMultiExtent = false;
        for (const auto& extent : targetExtents)
        {
            hasMultiExtent = hasMultiExtent || ((extent.flags & 0x80) != 0);
            totalLength += extent.dataLength;
        }
        if (!hasMultiExtent && !targetExtents.empty())
        {
            totalLength = targetExtents.front().dataLength;
            targetExtents = {targetExtents.front()};
        }
    }

    if (outError)
    {
        outError->clear();
    }

    std::error_code fsError;
    const std::filesystem::path parent = destination.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, fsError);
        if (fsError)
        {
            return setError("Failed to create destination directory: " + parent.string() + ": " +
                            fsError.message());
        }
    }

    tempDestination = destination;
    tempDestination +=
        ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    out.open(tempDestination, std::ios::binary);
    if (!out)
    {
        return setError("Failed to open temporary destination file: " + tempDestination.string());
    }
    wroteTempFile = true;

    u32 remainingTotal = totalLength;
    for (const auto& extent : targetExtents)
    {
        u32 remainingExtent = extent.dataLength;
        u32 sector = extent.extentLocation;
        while (remainingExtent > 0 && remainingTotal > 0)
        {
            const auto sectorData = readSector(sector);
            if (sectorData.empty())
            {
                return setError("Failed to read ISO sector while exporting: " + isoPath);
            }

            const u32 toWrite = std::min<u32>(
                {remainingExtent, remainingTotal, static_cast<u32>(sectorData.size())});
            out.write(reinterpret_cast<const char*>(sectorData.data()),
                      static_cast<std::streamsize>(toWrite));
            if (!out.good())
            {
                return setError("Failed to write destination file: " + destination.string());
            }

            remainingExtent -= toWrite;
            remainingTotal -= toWrite;
            ++sector;
        }
    }

    out.flush();
    if (!out.good())
    {
        return setError("Failed to flush destination file: " + destination.string());
    }

    out.close();
    if (!out.good())
    {
        return setError("Failed to close temporary destination file: " + tempDestination.string());
    }

    std::filesystem::rename(tempDestination, destination, fsError);
    if (fsError)
    {
        return setError("Failed to finalize destination file: " + destination.string() + ": " +
                        fsError.message());
    }
    wroteTempFile = false;

    return true;
}

bool IsoParser::readRangeFromIsoFile(const std::string& isoPath, u64 offset, size_t size,
                                     std::vector<u8>& outData, std::string* outError)
{
    auto setError = [&](const std::string& message)
    {
        addError(message);
        if (outError)
        {
            *outError = message;
        }
        return false;
    };

    if (!m_isOpen)
    {
        return setError("ISO parser is not open.");
    }

    if (!isIsoRelativePath(isoPath))
    {
        return setError("ISO path must be relative and must not contain traversal segments.");
    }

    DirectoryRecord target{};
    std::vector<DirectoryRecord> targetExtents;
    if (!findFileExtents(isoPath, target, targetExtents))
    {
        return setError("Failed to locate ISO file: " + isoPath);
    }

    std::sort(targetExtents.begin(), targetExtents.end(),
              [](const DirectoryRecord& lhs, const DirectoryRecord& rhs)
              { return lhs.extentLocation < rhs.extentLocation; });

    u64 totalLength = 0;
    if (targetExtents.size() == 1)
    {
        totalLength = static_cast<u64>(targetExtents.front().dataLength);
    }
    else
    {
        bool hasMultiExtent = false;
        for (const auto& extent : targetExtents)
        {
            hasMultiExtent = hasMultiExtent || ((extent.flags & 0x80) != 0);
            if (totalLength > std::numeric_limits<u64>::max() - static_cast<u64>(extent.dataLength))
            {
                return setError("ISO read range file size overflow: " + isoPath);
            }
            totalLength += static_cast<u64>(extent.dataLength);
        }
        if (!hasMultiExtent && !targetExtents.empty())
        {
            totalLength = static_cast<u64>(targetExtents.front().dataLength);
            targetExtents = {targetExtents.front()};
        }
    }

    if (offset > totalLength)
    {
        return setError("ISO read range offset exceeds file size: " + isoPath);
    }
    const u64 requestedSize = static_cast<u64>(size);
    if (requestedSize > totalLength - offset)
    {
        return setError("ISO read range exceeds file size: " + isoPath);
    }

    if (outError)
    {
        outError->clear();
    }
    outData.clear();
    outData.resize(size);
    if (size == 0)
    {
        return true;
    }

    u64 remainingSkip = offset;
    size_t writeOffset = 0;
    u64 remaining = requestedSize;

    for (const auto& extent : targetExtents)
    {
        if (remaining == 0)
        {
            break;
        }

        if (remainingSkip >= static_cast<u64>(extent.dataLength))
        {
            remainingSkip -= static_cast<u64>(extent.dataLength);
            continue;
        }
        u64 extentOffset = remainingSkip;
        remainingSkip = 0;

        while (extentOffset < static_cast<u64>(extent.dataLength) && remaining > 0)
        {
            const u64 sectorAdvance = extentOffset / kUserDataSize;
            const u32 sector = extent.extentLocation + static_cast<u32>(sectorAdvance);
            const auto sectorData = readSector(sector);
            if (sectorData.empty())
            {
                return setError("Failed to read ISO sector while extracting range: " + isoPath);
            }

            const size_t sectorOffset = static_cast<size_t>(extentOffset % kUserDataSize);
            if (sectorOffset >= sectorData.size())
            {
                return setError("Unexpected ISO sector offset while extracting range: " + isoPath);
            }

            const u64 availableInExtent = static_cast<u64>(extent.dataLength) - extentOffset;
            const size_t availableInSector = sectorData.size() - sectorOffset;
            const u64 toCopy =
                std::min<u64>({remaining, availableInExtent, static_cast<u64>(availableInSector)});
            if (toCopy == 0)
            {
                return setError("Failed to progress while extracting range: " + isoPath);
            }

            std::memcpy(outData.data() + static_cast<std::ptrdiff_t>(writeOffset),
                        sectorData.data() + static_cast<std::ptrdiff_t>(sectorOffset),
                        static_cast<size_t>(toCopy));

            writeOffset += static_cast<size_t>(toCopy);
            remaining -= toCopy;
            extentOffset += toCopy;
        }
    }

    if (remaining != 0)
    {
        return setError("Failed to satisfy full ISO read range: " + isoPath);
    }

    return true;
}

} // namespace iso
} // namespace psxrecomp
