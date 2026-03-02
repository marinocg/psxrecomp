#include "pipeline_candidate_test_helpers.h"

#include <cctype>
#include <cstdlib>

namespace pipeline_candidate_test_support
{

bool catalogEntryHasType(const std::string& catalog, const std::string& isoPath,
                         const std::string& type)
{
    const std::string isoMarker = "\"isoPath\": \"" + isoPath + "\"";
    const size_t isoPos = catalog.find(isoMarker);
    if (isoPos == std::string::npos)
    {
        return false;
    }

    const size_t nextIso = catalog.find("\"isoPath\": \"", isoPos + isoMarker.size());
    const std::string entrySlice =
        catalog.substr(isoPos, nextIso == std::string::npos ? std::string::npos : nextIso - isoPos);
    return entrySlice.find("\"" + type + "\"") != std::string::npos;
}

bool catalogEntryHasSha1(const std::string& catalog, const std::string& isoPath,
                         const std::string& sha1)
{
    const std::string isoMarker = "\"isoPath\": \"" + isoPath + "\"";
    const size_t isoPos = catalog.find(isoMarker);
    if (isoPos == std::string::npos)
    {
        return false;
    }

    const size_t nextIso = catalog.find("\"isoPath\": \"", isoPos + isoMarker.size());
    const std::string entrySlice =
        catalog.substr(isoPos, nextIso == std::string::npos ? std::string::npos : nextIso - isoPos);
    return entrySlice.find("\"sha1\": \"" + sha1 + "\"") != std::string::npos;
}

std::string findEmbeddedExportedPath(const std::string& catalog,
                                     const std::string& containerIsoPath)
{
    const std::string containerMarker = "\"containerIsoPath\": \"" + containerIsoPath + "\"";
    const size_t containerPos = catalog.find(containerMarker);
    if (containerPos == std::string::npos)
    {
        return "";
    }

    const std::string exportedMarker = "\"exportedPath\": \"";
    const size_t exportedPos = catalog.rfind(exportedMarker, containerPos);
    if (exportedPos == std::string::npos)
    {
        return "";
    }

    const size_t valueStart = exportedPos + exportedMarker.size();
    const size_t valueEnd = catalog.find('"', valueStart);
    if (valueEnd == std::string::npos)
    {
        return "";
    }
    return catalog.substr(valueStart, valueEnd - valueStart);
}

uint64_t findJsonIntegerField(const std::string& text, const std::string& fieldName)
{
    const std::string marker = "\"" + fieldName + "\": ";
    const size_t markerPos = text.find(marker);
    if (markerPos == std::string::npos)
    {
        return 0;
    }

    size_t numberStart = markerPos + marker.size();
    size_t numberEnd = numberStart;
    while (numberEnd < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[numberEnd])) != 0)
    {
        ++numberEnd;
    }
    if (numberEnd == numberStart)
    {
        return 0;
    }

    return static_cast<uint64_t>(
        std::strtoull(text.substr(numberStart, numberEnd - numberStart).c_str(), nullptr, 10));
}

} // namespace pipeline_candidate_test_support
