#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace pipeline_candidate_test_support
{

struct CueImagePaths
{
    std::filesystem::path cuePath;
    std::filesystem::path binPath;
};

bool catalogEntryHasType(const std::string& catalog, const std::string& isoPath,
                         const std::string& type);
bool catalogEntryHasSha1(const std::string& catalog, const std::string& isoPath,
                         const std::string& sha1);
std::string findEmbeddedExportedPath(const std::string& catalog,
                                     const std::string& containerIsoPath);
uint64_t findJsonIntegerField(const std::string& text, const std::string& fieldName);
std::filesystem::path createIsoWithExecutables(const std::string& label,
                                               const std::string& systemCnfContents);
std::filesystem::path createIsoWithTimBin(const std::string& label);
std::filesystem::path createIsoWithEmbeddedTimContainer(const std::string& label);
std::filesystem::path createIsoForExportPolicy(const std::string& label);
CueImagePaths createCueWithDataTrackOffset(const std::string& label, uint32_t trackStartLba);

} // namespace pipeline_candidate_test_support
