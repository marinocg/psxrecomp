#include "psxrecomp/iso/psx_exe_loader.h"

#include "psx_exe_loader_helpers.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace psxrecomp
{
namespace iso
{

namespace
{

constexpr size_t kTitleOffset = kPsxExeTitleOffset;
constexpr size_t kTitleLength = kPsxExeTitleLength;
constexpr size_t kReservedPrefixOffset = kPsxExeReservedPrefixOffset;
constexpr size_t kReservedPrefixSize = kPsxExeReservedPrefixSize;
constexpr size_t kReservedGapOffset = kPsxExeReservedGapOffset;
constexpr size_t kReservedGapSize = kPsxExeReservedGapSize;
constexpr size_t kSavedRegistersOffset = kPsxExeSavedRegistersOffset;
constexpr size_t kSavedRegistersSize = kPsxExeSavedRegistersSize;
constexpr size_t kTrailingDataOffset = kPsxExeTrailingDataOffset;
constexpr size_t kTrailingDataSize = kPsxExeTrailingDataSize;

u32 readLe32(const u8* data)
{
    return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
           (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
}


std::string trimString(const std::string& value)
{
    auto isTrimChar = [](unsigned char ch)
    { return ch == '\0' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };

    size_t start = 0;
    while (start < value.size() && isTrimChar(static_cast<unsigned char>(value[start])))
    {
        ++start;
    }
    if (start == value.size())
    {
        return "";
    }
    size_t end = value.size();
    while (end > start && isTrimChar(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }
    return value.substr(start, end - start);
}

u32 toPhysicalAddress(u32 address)
{
    return address & 0x1FFFFFFF;
}

bool isRangeInRam(u32 address, u32 size)
{
    if (size == 0)
    {
        return true;
    }
    u64 physicalStart = static_cast<u64>(toPhysicalAddress(address));
    u64 physicalEnd = physicalStart + static_cast<u64>(size);
    if (physicalEnd < physicalStart)
    {
        return false;
    }
    return physicalStart < MemoryMap::RAM_SIZE && physicalEnd <= MemoryMap::RAM_SIZE;
}

bool isAddressInRam(u32 address)
{
    return isRangeInRam(address, sizeof(u32));
}

bool isAligned(u32 address, u32 alignment)
{
    return alignment != 0 && (address % alignment) == 0;
}

void addDiagnostic(PsxExeDiagnostics* diagnostics, PsxExeDiagnosticSeverity severity,
                   PsxExeErrorCode code, const std::string& field, const std::string& message)
{
    if (!diagnostics)
    {
        return;
    }
    if (severity == PsxExeDiagnosticSeverity::Error)
    {
        diagnostics->addError(code, field, message);
    }
    else
    {
        diagnostics->addWarning(code, field, message);
    }
}

bool validateAlignedSegment(const std::string& field, u32 address, u32 size, u32 alignment,
                            PsxExeErrorCode addressCode, PsxExeErrorCode sizeCode,
                            PsxExeDiagnostics* diagnostics)
{
    if (size == 0)
    {
        return true;
    }
    bool ok = true;
    if (!isAligned(address, alignment))
    {
        addDiagnostic(diagnostics, PsxExeDiagnosticSeverity::Error, addressCode, field + "Address",
                      "Address is not aligned to " + std::to_string(alignment) + " bytes.");
        ok = false;
    }
    if (!isAligned(size, alignment))
    {
        addDiagnostic(diagnostics, PsxExeDiagnosticSeverity::Error, sizeCode, field + "Size",
                      "Size is not aligned to " + std::to_string(alignment) + " bytes.");
        ok = false;
    }
    return ok;
}

} // namespace

bool PsxExeLoader::parseHeader(const std::vector<u8>& data, PsxExeHeader& outHeader,
                               PsxExeDiagnostics* diagnostics)
{
    if (data.size() < kHeaderSize)
    {
        addDiagnostic(diagnostics, PsxExeDiagnosticSeverity::Error, PsxExeErrorCode::BufferTooSmall,
                      "header", "File is smaller than PSX-EXE header size.");
        return false;
    }

    if (std::memcmp(data.data(), "PS-X EXE", 8) != 0)
    {
        addDiagnostic(diagnostics, PsxExeDiagnosticSeverity::Error, PsxExeErrorCode::InvalidMagic,
                      "magic", "Missing PS-X EXE signature.");
        return false;
    }

    outHeader = {};
    std::copy_n(data.data() + kReservedPrefixOffset, kReservedPrefixSize,
                outHeader.reservedPrefix.begin());
    outHeader.initialPc = readLe32(data.data() + 0x10);
    outHeader.initialGp = readLe32(data.data() + 0x14);
    outHeader.loadAddress = readLe32(data.data() + 0x18);
    outHeader.loadSize = readLe32(data.data() + 0x1C);
    std::copy_n(data.data() + kReservedGapOffset, kReservedGapSize, outHeader.reservedGap.begin());
    outHeader.bssAddress = readLe32(data.data() + 0x28);
    outHeader.bssSize = readLe32(data.data() + 0x2C);
    outHeader.stackAddress = readLe32(data.data() + 0x30);
    outHeader.stackSize = readLe32(data.data() + 0x34);
    std::copy_n(data.data() + kSavedRegistersOffset, kSavedRegistersSize,
                outHeader.savedRegisters.begin());

    if (kTitleOffset + kTitleLength <= kHeaderSize)
    {
        std::string title(reinterpret_cast<const char*>(data.data() + kTitleOffset), kTitleLength);
        outHeader.title = trimString(title);
    }
    else
    {
        outHeader.title.clear();
    }

    std::copy_n(data.data() + kTrailingDataOffset, kTrailingDataSize,
                outHeader.trailingData.begin());

    return true;
}

bool PsxExeLoader::loadImage(const std::vector<u8>& data, PsxExeImage& outImage,
                             PsxExeDiagnostics* diagnostics)
{
    PsxExeDiagnostics localDiagnostics;
    PsxExeDiagnostics* activeDiagnostics = diagnostics ? diagnostics : &localDiagnostics;

    PsxExeHeader header{};
    if (!parseHeader(data, header, activeDiagnostics))
    {
        return false;
    }

    u32 effectiveLoadSize = header.loadSize;
    if (effectiveLoadSize == 0)
    {
        effectiveLoadSize = static_cast<u32>(data.size() - kHeaderSize);
        header.loadSize = effectiveLoadSize;
    }

    if (data.size() < kHeaderSize + effectiveLoadSize)
    {
        addDiagnostic(activeDiagnostics, PsxExeDiagnosticSeverity::Error,
                      PsxExeErrorCode::PayloadTooSmall, "loadSize",
                      "Program payload is smaller than header load size.");
        return false;
    }

    if (header.loadSize != 0 && data.size() != kHeaderSize + header.loadSize)
    {
        addDiagnostic(activeDiagnostics, PsxExeDiagnosticSeverity::Warning,
                      PsxExeErrorCode::LoadSizeMismatch, "loadSize",
                      "Header load size does not match file size.");
    }

    if (!isRangeInRam(header.loadAddress, effectiveLoadSize))
    {
        addDiagnostic(activeDiagnostics, PsxExeDiagnosticSeverity::Error,
                      PsxExeErrorCode::LoadAddressOutOfRange, "loadAddress",
                      "Load address range is outside PSX RAM.");
        return false;
    }

    validateAlignedSegment("load", header.loadAddress, effectiveLoadSize, 4,
                           PsxExeErrorCode::LoadAddressMisaligned,
                           PsxExeErrorCode::LoadSizeMisaligned, activeDiagnostics);

    if (header.initialPc != 0 &&
        (!isAddressInRam(header.initialPc) || !isAligned(header.initialPc, 4)))
    {
        addDiagnostic(activeDiagnostics, PsxExeDiagnosticSeverity::Error,
                      isAddressInRam(header.initialPc) ? PsxExeErrorCode::InitialPcMisaligned
                                                       : PsxExeErrorCode::InitialPcOutOfRange,
                      "initialPc", "Initial PC is invalid for PSX RAM.");
        return false;
    }

    if (header.initialGp != 0 &&
        (!isAddressInRam(header.initialGp) || !isAligned(header.initialGp, 4)))
    {
        addDiagnostic(activeDiagnostics, PsxExeDiagnosticSeverity::Error,
                      isAddressInRam(header.initialGp) ? PsxExeErrorCode::InitialGpMisaligned
                                                       : PsxExeErrorCode::InitialGpOutOfRange,
                      "initialGp", "Initial GP is invalid for PSX RAM.");
        return false;
    }

    if (header.bssSize != 0 && !isRangeInRam(header.bssAddress, header.bssSize))
    {
        addDiagnostic(activeDiagnostics, PsxExeDiagnosticSeverity::Error,
                      PsxExeErrorCode::BssOutOfRange, "bssAddress",
                      "BSS range is outside PSX RAM.");
        return false;
    }

    if (header.bssSize != 0)
    {
        validateAlignedSegment("bss", header.bssAddress, header.bssSize, 4,
                               PsxExeErrorCode::BssMisaligned, PsxExeErrorCode::BssSizeMisaligned,
                               activeDiagnostics);
    }

    if (header.stackSize != 0 && !isRangeInRam(header.stackAddress, header.stackSize))
    {
        addDiagnostic(activeDiagnostics, PsxExeDiagnosticSeverity::Error,
                      PsxExeErrorCode::StackOutOfRange, "stackAddress",
                      "Stack range is outside PSX RAM.");
        return false;
    }

    if (header.stackSize != 0)
    {
        validateAlignedSegment("stack", header.stackAddress, header.stackSize, 4,
                               PsxExeErrorCode::StackMisaligned,
                               PsxExeErrorCode::StackSizeMisaligned, activeDiagnostics);
    }

    if (activeDiagnostics->hasErrors())
    {
        return false;
    }

    outImage.header = header;
    outImage.entryPoint = {header.initialPc, header.initialGp, header.stackAddress};
    outImage.programData.assign(data.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                                data.begin() +
                                    static_cast<std::ptrdiff_t>(kHeaderSize + effectiveLoadSize));
    outImage.segments.clear();
    PsxExeImage::Segment mainSegment;
    mainSegment.loadAddress = header.loadAddress;
    mainSegment.size = effectiveLoadSize;
    mainSegment.fileOffset = static_cast<u32>(kHeaderSize);
    mainSegment.data = outImage.programData;
    outImage.segments.push_back(std::move(mainSegment));

    if (!detail::parseOverlayTable(data, header, outImage.segments, activeDiagnostics))
    {
        return false;
    }

    outImage.syscalls.clear();
    detail::extractSyscalls(outImage.segments, outImage.syscalls);
    outImage.symbols.clear();
    detail::buildDefaultSymbols(outImage, outImage.symbols);
    return true;
}

bool PsxExeLoader::loadMemoryImage(const std::vector<u8>& data, PsxExeMemoryImage& outImage,
                                   PsxExeDiagnostics* diagnostics)
{
    PsxExeImage image{};
    if (!loadImage(data, image, diagnostics))
    {
        return false;
    }

    outImage.header = image.header;
    outImage.entryPoint = image.entryPoint;
    outImage.ram.assign(static_cast<size_t>(MemoryMap::RAM_SIZE), 0);

    for (const auto& segment : image.segments)
    {
        u32 loadOffset = toPhysicalAddress(segment.loadAddress);
        std::copy(segment.data.begin(), segment.data.end(),
                  outImage.ram.begin() + static_cast<size_t>(loadOffset));
    }

    if (image.header.bssSize != 0)
    {
        u32 bssOffset = toPhysicalAddress(image.header.bssAddress);
        std::fill(outImage.ram.begin() + static_cast<size_t>(bssOffset),
                  outImage.ram.begin() + static_cast<size_t>(bssOffset + image.header.bssSize), 0);
    }

    return true;
}

bool PsxExeLoader::loadFromFile(const std::string& filename, PsxExeImage& outImage,
                                PsxExeDiagnostics* diagnostics)
{
    PsxExeDiagnostics localDiagnostics;
    PsxExeDiagnostics* activeDiagnostics = diagnostics ? diagnostics : &localDiagnostics;

    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file)
    {
        addDiagnostic(activeDiagnostics, PsxExeDiagnosticSeverity::Error,
                      PsxExeErrorCode::FileOpenFailed, "file", "Failed to open PSX-EXE file.");
        return false;
    }

    std::streamsize size = file.tellg();
    if (size <= 0)
    {
        addDiagnostic(activeDiagnostics, PsxExeDiagnosticSeverity::Error,
                      PsxExeErrorCode::FileEmpty, "file", "PSX-EXE file is empty.");
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<u8> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        addDiagnostic(activeDiagnostics, PsxExeDiagnosticSeverity::Error,
                      PsxExeErrorCode::FileReadFailed, "file", "Failed to read PSX-EXE file.");
        return false;
    }

    return loadImage(buffer, outImage, diagnostics);
}

void PsxExeLoader::exportSymbols(const PsxExeImage& image, const SymbolCallback& callback)
{
    if (!callback)
    {
        return;
    }
    for (const auto& symbol : image.symbols)
    {
        callback(symbol);
    }
}

} // namespace iso
} // namespace psxrecomp
