#pragma once

#include "psxrecomp/types.h"
#include <string>
#include <vector>

namespace psxrecomp
{
namespace iso
{

/**
 * @brief Parsed PSX-EXE header fields.
 */
struct PsxExeHeader
{
    std::string title;
    u32 initialPc;
    u32 initialGp;
    u32 loadAddress;
    u32 loadSize;
    u32 bssAddress;
    u32 bssSize;
    u32 stackAddress;
    u32 stackSize;
};

/**
 * @brief Loaded PSX-EXE image.
 */
struct PsxExeImage
{
    PsxExeHeader header;
    std::vector<u8> programData;
};

/**
 * @brief PSX-EXE loader and parser.
 */
class PsxExeLoader
{
  public:
    static constexpr size_t kHeaderSize = 2048;

    /**
     * @brief Parse a PSX-EXE header from a buffer.
     * @param data File contents.
     * @param outHeader Parsed header output.
     * @return true if the header is valid.
     */
    static bool parseHeader(const std::vector<u8>& data, PsxExeHeader& outHeader);

    /**
     * @brief Load a PSX-EXE image from a buffer.
     * @param data File contents.
     * @param outImage Parsed image output.
     * @return true if the image loads successfully.
     */
    static bool loadImage(const std::vector<u8>& data, PsxExeImage& outImage);

    /**
     * @brief Load a PSX-EXE image from disk.
     * @param filename Path to PSX-EXE file.
     * @param outImage Parsed image output.
     * @return true if the image loads successfully.
     */
    static bool loadFromFile(const std::string& filename, PsxExeImage& outImage);
};

} // namespace iso
} // namespace psxrecomp
