#include "cue_sheet.h"

#include "iso_utils.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace psxrecomp
{
namespace iso
{

namespace
{

struct ParsedTrackType
{
    TrackType type = TrackType::Unknown;
    u32 sectorSize = 0;
};

ParsedTrackType parseTrackType(const std::string& type)
{
    auto upper = detail::toUpper(type);
    if (upper == "AUDIO")
    {
        return {TrackType::Audio, 2352};
    }
    if (upper == "MODE1/2048")
    {
        return {TrackType::Data, 2048};
    }
    if (upper == "MODE2/2352")
    {
        return {TrackType::Data, 2352};
    }
    if (upper == "MODE2/2048")
    {
        return {TrackType::Data, 2048};
    }
    return {};
}

bool parseTimecode(const std::string& timecode, u32& outLba)
{
    int minutes = 0;
    int seconds = 0;
    int frames = 0;
    char separator = '\0';
    std::istringstream timeStream(timecode);
    if (!(timeStream >> minutes >> separator >> seconds >> separator >> frames))
    {
        return false;
    }
    int lba = (minutes * 60 * 75 + seconds * 75 + frames) - 150;
    if (lba < 0)
    {
        lba = 0;
    }
    outLba = static_cast<u32>(lba);
    return true;
}

bool parseTimecodeLength(const std::string& timecode, u32& outLength)
{
    int minutes = 0;
    int seconds = 0;
    int frames = 0;
    char separator = '\0';
    std::istringstream timeStream(timecode);
    if (!(timeStream >> minutes >> separator >> seconds >> separator >> frames))
    {
        return false;
    }
    int lba = minutes * 60 * 75 + seconds * 75 + frames;
    if (lba < 0)
    {
        lba = 0;
    }
    outLength = static_cast<u32>(lba);
    return true;
}

void updatePregap(TrackInfo& track, bool hasIndex00, u32 index00Lba)
{
    if (!hasIndex00)
    {
        return;
    }
    if (track.startLba >= index00Lba)
    {
        track.pregapLength = track.startLba - index00Lba;
    }
}

} // namespace

const TrackInfo* CueSheet::findFirstDataTrack() const
{
    for (const auto& track : tracks)
    {
        if (track.type == TrackType::Data)
        {
            return &track;
        }
    }
    return nullptr;
}

const TrackInfo* CueSheet::findPrimaryDataTrack() const
{
    const TrackInfo* selected = nullptr;
    for (const auto& track : tracks)
    {
        if (track.type != TrackType::Data)
        {
            continue;
        }
        if (!selected)
        {
            selected = &track;
            continue;
        }
        if (track.sessionNumber > selected->sessionNumber)
        {
            selected = &track;
            continue;
        }
        if (track.sessionNumber == selected->sessionNumber && track.startLba > selected->startLba)
        {
            selected = &track;
        }
    }
    return selected;
}

bool parseCueSheet(const std::string& cuePath, CueSheet& outSheet, std::string& errorMessage)
{
    outSheet.tracks.clear();
    errorMessage.clear();

    std::filesystem::path path(cuePath);
    if (!path.has_extension() || detail::toUpper(path.extension().string()) != ".CUE")
    {
        return false;
    }

    std::ifstream cueStream(cuePath);
    if (!cueStream)
    {
        errorMessage = "Failed to open CUE sheet: " + cuePath;
        return false;
    }

    std::string line;
    std::string currentFile;
    TrackInfo* currentTrack = nullptr;
    u32 currentSession = 1;
    bool hasIndex00 = false;
    u32 index00Lba = 0;

    while (std::getline(cueStream, line))
    {
        auto trimmed = detail::trim(line);
        if (trimmed.empty())
        {
            continue;
        }
        auto upper = detail::toUpper(trimmed);
        if (upper.rfind("REM SESSION", 0) == 0 || upper.rfind("SESSION", 0) == 0)
        {
            std::istringstream stream(trimmed);
            std::string token;
            std::string sessionToken;
            stream >> token;
            if (detail::toUpper(token) == "REM")
            {
                stream >> token;
            }
            stream >> sessionToken;
            u32 sessionValue = 0;
            auto sessionStart = sessionToken.data();
            auto sessionEnd = sessionToken.data() + sessionToken.size();
            auto parseResult = std::from_chars(sessionStart, sessionEnd, sessionValue);
            if (parseResult.ec == std::errc() && parseResult.ptr == sessionEnd)
            {
                currentSession = sessionValue;
            }
            continue;
        }
        if (upper.rfind("FILE", 0) == 0)
        {
            auto firstQuote = trimmed.find('"');
            auto lastQuote = trimmed.find_last_of('"');
            if (firstQuote != std::string::npos && lastQuote != std::string::npos &&
                lastQuote > firstQuote)
            {
                currentFile = trimmed.substr(firstQuote + 1, lastQuote - firstQuote - 1);
            }
            else
            {
                std::istringstream stream(trimmed);
                std::string token;
                std::string fileToken;
                std::string fileTypeToken;
                stream >> token >> fileToken >> fileTypeToken;
                currentFile = fileToken;
            }
            continue;
        }
        if (upper.rfind("TRACK", 0) == 0)
        {
            std::istringstream stream(trimmed);
            std::string token;
            std::string number;
            std::string typeToken;
            stream >> token >> number >> typeToken;
            auto trackType = parseTrackType(typeToken);
            if (trackType.type == TrackType::Unknown || trackType.sectorSize == 0)
            {
                errorMessage = "Unsupported track type in CUE sheet: " + typeToken;
                return false;
            }
            u32 trackNumber = 0;
            auto numberStart = number.data();
            auto numberEnd = number.data() + number.size();
            auto parseResult = std::from_chars(numberStart, numberEnd, trackNumber);
            if (parseResult.ec != std::errc() || parseResult.ptr != numberEnd)
            {
                errorMessage = "Invalid track number in CUE sheet: " + number;
                return false;
            }
            TrackInfo track{};
            track.trackNumber = trackNumber;
            track.type = trackType.type;
            track.sectorSize = trackType.sectorSize;
            track.startLba = 0;
            track.sessionNumber = currentSession;
            track.pregapLength = 0;
            track.file = currentFile;
            outSheet.tracks.push_back(track);
            currentTrack = &outSheet.tracks.back();
            hasIndex00 = false;
            index00Lba = 0;
            continue;
        }
        if (upper.rfind("PREGAP", 0) == 0 && currentTrack)
        {
            std::istringstream stream(trimmed);
            std::string token;
            std::string timecode;
            stream >> token >> timecode;
            u32 pregapLength = 0;
            if (parseTimecodeLength(timecode, pregapLength))
            {
                currentTrack->pregapLength = pregapLength;
            }
            continue;
        }
        if (upper.rfind("INDEX 00", 0) == 0 && currentTrack)
        {
            std::istringstream stream(trimmed);
            std::string token;
            std::string indexToken;
            std::string timecode;
            stream >> token >> indexToken >> timecode;
            u32 lba = 0;
            if (parseTimecode(timecode, lba))
            {
                hasIndex00 = true;
                index00Lba = lba;
                updatePregap(*currentTrack, hasIndex00, index00Lba);
            }
            continue;
        }
        if (upper.rfind("INDEX 01", 0) == 0 && currentTrack)
        {
            std::istringstream stream(trimmed);
            std::string token;
            std::string indexToken;
            std::string timecode;
            stream >> token >> indexToken >> timecode;
            u32 lba = 0;
            if (parseTimecode(timecode, lba))
            {
                currentTrack->startLba = lba;
                updatePregap(*currentTrack, hasIndex00, index00Lba);
            }
            continue;
        }
    }

    if (outSheet.tracks.empty())
    {
        errorMessage = "CUE sheet has no tracks: " + cuePath;
        return false;
    }

    for (const auto& track : outSheet.tracks)
    {
        if (track.file.empty())
        {
            errorMessage = "CUE sheet track is missing file association.";
            return false;
        }
    }

    return true;
}

} // namespace iso
} // namespace psxrecomp
