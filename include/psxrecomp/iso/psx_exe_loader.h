#pragma once

#include "psxrecomp/types.h"
#include <array>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace iso
{

constexpr size_t kPsxExeHeaderSize = 2048;
constexpr size_t kPsxExeReservedPrefixOffset = 0x08;
constexpr size_t kPsxExeReservedPrefixSize = 0x08;
constexpr size_t kPsxExeReservedGapOffset = 0x20;
constexpr size_t kPsxExeReservedGapSize = 0x08;
constexpr size_t kPsxExeSavedRegistersOffset = 0x38;
constexpr size_t kPsxExeSavedRegistersSize = 0x14;
constexpr size_t kPsxExeTitleOffset = 0x4C;
constexpr size_t kPsxExeTitleLength = 60;
constexpr size_t kPsxExeTrailingDataOffset = kPsxExeTitleOffset + kPsxExeTitleLength;
constexpr size_t kPsxExeTrailingDataSize = kPsxExeHeaderSize - kPsxExeTrailingDataOffset;

enum class PsxExeDiagnosticSeverity
{
    Error,
    Warning
};

enum class PsxExeErrorCode
{
    BufferTooSmall,
    FileOpenFailed,
    FileEmpty,
    FileReadFailed,
    InvalidMagic,
    PayloadTooSmall,
    LoadAddressOutOfRange,
    LoadAddressMisaligned,
    LoadSizeMisaligned,
    LoadSizeMismatch,
    InitialPcOutOfRange,
    InitialPcMisaligned,
    InitialGpOutOfRange,
    InitialGpMisaligned,
    BssOutOfRange,
    BssMisaligned,
    BssSizeMisaligned,
    StackOutOfRange,
    StackMisaligned,
    StackSizeMisaligned
};

struct PsxExeDiagnostic
{
    PsxExeDiagnosticSeverity severity;
    PsxExeErrorCode code;
    std::string field;
    std::string message;
};

struct PsxExeDiagnostics
{
    std::vector<PsxExeDiagnostic> entries;

    void addError(PsxExeErrorCode code, const std::string& field, const std::string& message)
    {
        entries.push_back({PsxExeDiagnosticSeverity::Error, code, field, message});
    }

    void addWarning(PsxExeErrorCode code, const std::string& field, const std::string& message)
    {
        entries.push_back({PsxExeDiagnosticSeverity::Warning, code, field, message});
    }

    bool hasErrors() const
    {
        for (const auto& entry : entries)
        {
            if (entry.severity == PsxExeDiagnosticSeverity::Error)
            {
                return true;
            }
        }
        return false;
    }
};

struct PsxExeEntryPoint
{
    u32 pc = 0;
    u32 gp = 0;
    u32 sp = 0;
};

/**
 * @brief Parsed PSX-EXE header fields.
 */
struct PsxExeHeader
{
    std::string title;
    std::array<u8, kPsxExeReservedPrefixSize> reservedPrefix{};
    u32 initialPc;
    u32 initialGp;
    u32 loadAddress;
    u32 loadSize;
    std::array<u8, kPsxExeReservedGapSize> reservedGap{};
    u32 bssAddress;
    u32 bssSize;
    u32 stackAddress;
    u32 stackSize;
    std::array<u8, kPsxExeSavedRegistersSize> savedRegisters{};
    std::array<u8, kPsxExeTrailingDataSize> trailingData{};
};

/**
 * @brief Loaded PSX-EXE image.
 */
struct PsxExeImage
{
    PsxExeHeader header;
    PsxExeEntryPoint entryPoint;
    std::vector<u8> programData;
};

/**
 * @brief Loaded PSX-EXE memory image.
 */
struct PsxExeMemoryImage
{
    PsxExeHeader header;
    PsxExeEntryPoint entryPoint;
    std::vector<u8> ram;
};

/**
 * @brief PSX-EXE loader and parser.
 */
class PsxExeLoader
{
  public:
    static constexpr size_t kHeaderSize = kPsxExeHeaderSize;

    /**
     * @brief Parse a PSX-EXE header from a buffer.
     * @param data File contents.
     * @param outHeader Parsed header output.
     * @param diagnostics Optional diagnostics output.
     * @return true if the header is valid.
     */
    static bool parseHeader(const std::vector<u8>& data, PsxExeHeader& outHeader,
                            PsxExeDiagnostics* diagnostics = nullptr);

    /**
     * @brief Load a PSX-EXE image from a buffer.
     * @param data File contents.
     * @param outImage Parsed image output.
     * @param diagnostics Optional diagnostics output.
     * @return true if the image loads successfully.
     */
    static bool loadImage(const std::vector<u8>& data, PsxExeImage& outImage,
                          PsxExeDiagnostics* diagnostics = nullptr);

    /**
     * @brief Load a PSX-EXE image into a memory image buffer.
     * @param data File contents.
     * @param outImage Parsed memory image output.
     * @param diagnostics Optional diagnostics output.
     * @return true if the image loads successfully.
     */
    static bool loadMemoryImage(const std::vector<u8>& data, PsxExeMemoryImage& outImage,
                                PsxExeDiagnostics* diagnostics = nullptr);

    /**
     * @brief Load a PSX-EXE image from disk.
     * @param filename Path to PSX-EXE file.
     * @param outImage Parsed image output.
     * @param diagnostics Optional diagnostics output.
     * @return true if the image loads successfully.
     */
    static bool loadFromFile(const std::string& filename, PsxExeImage& outImage,
                             PsxExeDiagnostics* diagnostics = nullptr);
};

} // namespace iso
} // namespace psxrecomp
