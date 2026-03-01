#pragma once

#include <cstdint>
#include <filesystem>

std::filesystem::path createRawIsoFromPlain(const std::filesystem::path& plainIsoPath,
                                            uint32_t rawSectorSize, uint32_t userDataOffset,
                                            uint8_t modeByte);

std::filesystem::path createPlainIsoWithXaExtension();
std::filesystem::path createIsoWithBrokenPathTableAndNestedTim();

struct SplitCueImage
{
    std::filesystem::path cuePath;
    std::filesystem::path dataTrackPath;
    std::filesystem::path audioTrackPath;
    uint32_t totalDiscSectors = 0;
};

SplitCueImage createSplitCueWithGlobalVolumeSpace();
