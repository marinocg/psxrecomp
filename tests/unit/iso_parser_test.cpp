#include "psxrecomp/iso/iso_parser.h"
#include "psxrecomp/iso/multi_disc_set.h"

#include "iso_test_helpers.h"

#include <algorithm>
#include <cassert>
#include <filesystem>

using iso_test::createCueImage;
using iso_test::createMultiSessionCue;
using iso_test::createTestIso;
using iso_test::createTestIsoWithLabel;
using iso_test::kSectorSize;

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

    auto exportDir = std::filesystem::temp_directory_path() /
                     ("psxrecomp_exports_" + std::to_string(iso_test::generateUniqueSuffix()));
    assert(cueParser.exportResources(psxrecomp::iso::ResourceType::TimTexture, exportDir.string()));
    assert(std::filesystem::exists(exportDir / "TEXTURE.TIM"));

    std::error_code cleanupError;
    std::filesystem::remove_all(exportDir, cleanupError);

    std::filesystem::remove(cuePath);
    std::filesystem::remove(binPath);

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
