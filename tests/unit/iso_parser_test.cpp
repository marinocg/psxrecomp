#include "psxrecomp/iso/iso_parser.h"
#include "psxrecomp/iso/multi_disc_set.h"

#include "iso_test_helpers.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using iso_test::createCueImage;
using iso_test::createMultiSessionCue;
using iso_test::createTestIso;
using iso_test::createTestIsoWithLabel;
using iso_test::kSectorSize;

#include "iso_parser_test_helpers_extra.h"

int main()
{
    auto isoPath = createTestIso();

    psxrecomp::iso::IsoParser parser(isoPath.string());
    assert(parser.open());
    assert(parser.isValid());
    assert(parser.getVolumeLabel() == "PSXRECOMP_TEST");

    auto systemCnf = parser.extractFile("SYSTEM.CNF");
    assert(!systemCnf.empty());

    auto exeName = parser.findExecutable();
    assert(exeName == "DATA/GAME.EXE");

    auto executableList = parser.listExecutables();
    assert(std::find(executableList.begin(), executableList.end(), exeName) !=
           executableList.end());
    auto flatTree = parser.listAllFilesRecursive();
    auto dataDirEntry =
        std::find_if(flatTree.begin(), flatTree.end(), [](const psxrecomp::iso::IsoFileEntry& entry)
                     { return entry.path == "DATA" && entry.isDirectory; });
    assert(dataDirEntry != flatTree.end());
    auto gameExeEntry =
        std::find_if(flatTree.begin(), flatTree.end(), [](const psxrecomp::iso::IsoFileEntry& entry)
                     { return entry.path == "DATA/GAME.EXE" && !entry.isDirectory; });
    assert(gameExeEntry != flatTree.end());
    assert(gameExeEntry->size == 16);
    assert(gameExeEntry->extents.size() == 1);

    auto tracks = parser.getTracks();
    assert(tracks.size() == 1);
    assert(tracks.front().type == psxrecomp::iso::TrackType::Data);
    assert(tracks.front().startLba == 0);

    std::filesystem::remove(isoPath);

    std::filesystem::path cuePath;
    auto binPath = createCueImage(cuePath);
    psxrecomp::iso::IsoParser cueParser(cuePath.string());
    assert(cueParser.open());
    assert(cueParser.isValid());
    assert(cueParser.findExecutable() == "GAME.EXE");

    auto cueTracks = cueParser.getTracks();
    assert(cueTracks.size() == 1);
    assert(cueTracks.front().type == psxrecomp::iso::TrackType::Data);
    assert(cueTracks.front().startLba == 0);
    assert(cueTracks.front().file == binPath.string());

    auto multiExtent = cueParser.extractFile("MULTI.BIN");
    assert(multiExtent.size() == kSectorSize * 2);
    assert(multiExtent.front() == 'A');
    assert(multiExtent.back() == 'B');

    auto xaData = cueParser.extractFile("AUDIO.XA");
    assert(xaData.size() == 2324);
    assert(xaData.front() == 'X');

    auto jolietData = cueParser.extractFile("LONGNAME.TXT");
    assert(!jolietData.empty());

    auto timResources = cueParser.listResources(psxrecomp::iso::ResourceType::TimTexture);
    assert(timResources.size() == 1);
    assert(timResources.front() == "TEXTURE.TIM");

    auto strResources = cueParser.listResources(psxrecomp::iso::ResourceType::StrVideo);
    assert(strResources.size() == 1);
    assert(strResources.front() == "MOVIE.STR");

    auto xaResources = cueParser.listResources(psxrecomp::iso::ResourceType::XaAudio);
    assert(xaResources.size() == 1);
    assert(xaResources.front() == "AUDIO.XA");
    auto cueTree = cueParser.listAllFilesRecursive();
    auto multiEntry =
        std::find_if(cueTree.begin(), cueTree.end(), [](const psxrecomp::iso::IsoFileEntry& entry)
                     { return entry.path == "MULTI.BIN" && !entry.isDirectory; });
    assert(multiEntry != cueTree.end());
    assert(multiEntry->size == kSectorSize * 2);
    assert(multiEntry->extents.size() == 2);
    assert(multiEntry->extents.front().continues);
    assert(!multiEntry->extents.back().continues);

    auto plainXaIso = createPlainIsoWithXaExtension();
    psxrecomp::iso::IsoParser plainXaParser(plainXaIso.string());
    assert(plainXaParser.open());
    assert(plainXaParser.isValid());
    auto plainXaResources = plainXaParser.listResources(psxrecomp::iso::ResourceType::XaAudio);
    assert(plainXaResources.size() == 1);
    assert(plainXaResources.front() == "AUDIO.XA");
    std::filesystem::remove(plainXaIso);

    auto brokenPathTableIso = createIsoWithBrokenPathTableAndNestedTim();
    psxrecomp::iso::IsoParser brokenPathTableParser(brokenPathTableIso.string());
    assert(brokenPathTableParser.open());
    assert(brokenPathTableParser.isValid());
    auto brokenTimResources =
        brokenPathTableParser.listResources(psxrecomp::iso::ResourceType::TimTexture);
    assert(brokenTimResources.size() == 1);
    assert(brokenTimResources.front() == "DATA/NESTED.TIM");
    auto brokenTree = brokenPathTableParser.listAllFilesRecursive();
    auto nestedTimEntry = std::find_if(
        brokenTree.begin(), brokenTree.end(), [](const psxrecomp::iso::IsoFileEntry& entry)
        { return entry.path == "DATA/NESTED.TIM" && !entry.isDirectory; });
    assert(nestedTimEntry != brokenTree.end());
    bool hasPathTableError = false;
    for (const auto& error : brokenPathTableParser.getErrors())
    {
        if (error.find("path table") != std::string::npos ||
            error.find("Path table") != std::string::npos)
        {
            hasPathTableError = true;
            break;
        }
    }
    assert(hasPathTableError);
    std::filesystem::remove(brokenPathTableIso);

    auto plainForRaw2352Mode1 = createTestIso();
    auto raw2352Mode1 = createRawIsoFromPlain(plainForRaw2352Mode1, 2352, 16, 1);
    psxrecomp::iso::IsoParser rawMode1Parser(raw2352Mode1.string());
    assert(rawMode1Parser.open());
    assert(rawMode1Parser.isValid());
    assert(rawMode1Parser.findExecutable() == "DATA/GAME.EXE");
    std::filesystem::remove(raw2352Mode1);
    std::filesystem::remove(plainForRaw2352Mode1);

    auto plainForRaw2352Mode2 = createTestIso();
    auto raw2352Mode2 = createRawIsoFromPlain(plainForRaw2352Mode2, 2352, 24, 2);
    psxrecomp::iso::IsoParser rawMode2Parser(raw2352Mode2.string());
    assert(rawMode2Parser.open());
    assert(rawMode2Parser.isValid());
    assert(rawMode2Parser.findExecutable() == "DATA/GAME.EXE");
    std::filesystem::remove(raw2352Mode2);
    std::filesystem::remove(plainForRaw2352Mode2);

    auto plainForRaw2336 = createTestIso();
    auto raw2336 = createRawIsoFromPlain(plainForRaw2336, 2336, 0, 0);
    psxrecomp::iso::IsoParser raw2336Parser(raw2336.string());
    assert(raw2336Parser.open());
    assert(raw2336Parser.isValid());
    assert(raw2336Parser.findExecutable() == "DATA/GAME.EXE");
    std::filesystem::remove(raw2336);
    std::filesystem::remove(plainForRaw2336);

    auto exportDir = std::filesystem::temp_directory_path() /
                     ("psxrecomp_exports_" + std::to_string(iso_test::generateUniqueSuffix()));
    assert(cueParser.exportResources(psxrecomp::iso::ResourceType::TimTexture, exportDir.string()));
    assert(std::filesystem::exists(exportDir / "TEXTURE.TIM"));

    std::error_code cleanupError;
    std::filesystem::remove_all(exportDir, cleanupError);

    std::filesystem::remove(cuePath);
    std::filesystem::remove(binPath);

    auto splitCue = createSplitCueWithGlobalVolumeSpace();
    psxrecomp::iso::IsoParser splitCueParser(splitCue.cuePath.string());
    assert(splitCueParser.open());
    assert(splitCueParser.isValid());
    assert(splitCueParser.findExecutable() == "GAME.EXE");
    assert(splitCueParser.getTotalSectors() == splitCue.totalDiscSectors);
    bool hasVolumeSizeError = false;
    for (const auto& error : splitCueParser.getErrors())
    {
        if (error.find("Volume space size exceeds image size.") != std::string::npos)
        {
            hasVolumeSizeError = true;
            break;
        }
    }
    assert(!hasVolumeSizeError);
    std::filesystem::remove(splitCue.cuePath);
    std::filesystem::remove(splitCue.dataTrackPath);
    std::filesystem::remove(splitCue.audioTrackPath);

    std::filesystem::path multiCuePath;
    std::filesystem::path sessionTwoBin;
    auto sessionOneBin = createMultiSessionCue(multiCuePath, sessionTwoBin);
    psxrecomp::iso::IsoParser multiParser(multiCuePath.string());
    assert(multiParser.open());
    assert(multiParser.isValid());
    assert(multiParser.getVolumeLabel() == "SESSION_TWO");
    auto multiTracks = multiParser.getTracks();
    assert(multiTracks.size() == 2);
    auto primaryTrack = multiParser.getDataTrack();
    assert(primaryTrack.has_value());
    assert(primaryTrack->sessionNumber == 2);
    assert(multiTracks[1].pregapLength == 150);

    std::filesystem::remove(multiCuePath);
    std::filesystem::remove(sessionOneBin);
    std::filesystem::remove(sessionTwoBin);

    auto isoA = createTestIsoWithLabel("DISC_ONE");
    auto isoB = createTestIsoWithLabel("DISC_TWO");
    psxrecomp::iso::MultiDiscSet discSet({isoA.string(), isoB.string()});
    assert(discSet.open());
    assert(discSet.getDiscCount() == 2);
    assert(discSet.getDiscInfo(1).volumeLabel == "DISC_TWO");
    auto cumulativeTracks = discSet.getCumulativeTracks();
    assert(!cumulativeTracks.empty());
    assert(cumulativeTracks.front().discIndex == 0);
    assert(discSet.setActiveDisc(1));
    assert(discSet.getActiveDiscIndex() == 1);

    std::filesystem::remove(isoA);
    std::filesystem::remove(isoB);
    return 0;
}
