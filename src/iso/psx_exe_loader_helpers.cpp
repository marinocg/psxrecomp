#include "psx_exe_loader_helpers.h"

#include <cstring>
#include <sstream>

namespace psxrecomp
{
namespace iso
{
namespace detail
{
namespace
{
constexpr size_t kOverlayMagicSize = 4;
constexpr char kOverlayMagic[kOverlayMagicSize] = {'O', 'V', 'L', 'Y'};
constexpr size_t kOverlayHeaderSize = kOverlayMagicSize + sizeof(u32);
constexpr size_t kOverlayEntrySize = sizeof(u32) * 3;

u32 readLe32(const u8* data)
{
    return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
           (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
}

u32 readLe32At(const std::vector<u8>& data, size_t offset)
{
    return readLe32(data.data() + offset);
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

bool validateOverlayEntry(u32 loadAddress, u32 size, PsxExeDiagnostics* diagnostics)
{
    if (size == 0)
    {
        addDiagnostic(diagnostics, PsxExeDiagnosticSeverity::Error,
                      PsxExeErrorCode::OverlayTableMalformed, "overlaySize",
                      "Overlay size must be non-zero.");
        return false;
    }
    if (!isRangeInRam(loadAddress, size))
    {
        addDiagnostic(diagnostics, PsxExeDiagnosticSeverity::Error,
                      PsxExeErrorCode::OverlayEntryOutOfRange, "overlayAddress",
                      "Overlay range is outside PSX RAM.");
        return false;
    }
    if (!validateAlignedSegment("overlay", loadAddress, size, 4,
                                PsxExeErrorCode::OverlayEntryMisaligned,
                                PsxExeErrorCode::OverlayEntryMisaligned, diagnostics))
    {
        return false;
    }
    return true;
}

} // namespace

bool parseOverlayTable(const std::vector<u8>& data, const PsxExeHeader& header,
                       std::vector<PsxExeImage::Segment>& segments, PsxExeDiagnostics* diagnostics)
{
    if (header.trailingData.size() < kOverlayHeaderSize ||
        std::memcmp(header.trailingData.data(), kOverlayMagic, kOverlayMagicSize) != 0)
    {
        return true;
    }

    const u32 overlayCount = readLe32(header.trailingData.data() + kOverlayMagicSize);
    const size_t maxEntries = (header.trailingData.size() - kOverlayHeaderSize) / kOverlayEntrySize;
    if (overlayCount > maxEntries)
    {
        addDiagnostic(diagnostics, PsxExeDiagnosticSeverity::Error,
                      PsxExeErrorCode::OverlayTableMalformed, "overlayTable",
                      "Overlay table entry count exceeds trailing header storage.");
        return false;
    }
    size_t tableOffset = kOverlayHeaderSize;
    for (u32 index = 0; index < overlayCount; ++index)
    {
        const u32 loadAddress = readLe32(header.trailingData.data() + tableOffset);
        const u32 size = readLe32(header.trailingData.data() + tableOffset + sizeof(u32));
        const u32 fileOffset = readLe32(header.trailingData.data() + tableOffset + sizeof(u32) * 2);
        tableOffset += kOverlayEntrySize;

        if (!validateOverlayEntry(loadAddress, size, diagnostics))
        {
            return false;
        }

        const size_t endOffset = static_cast<size_t>(fileOffset) + static_cast<size_t>(size);
        if (endOffset > data.size())
        {
            addDiagnostic(diagnostics, PsxExeDiagnosticSeverity::Error,
                          PsxExeErrorCode::OverlayEntryOutOfRange, "overlayFileOffset",
                          "Overlay data range is outside the file payload.");
            return false;
        }

        PsxExeImage::Segment overlaySegment;
        overlaySegment.loadAddress = loadAddress;
        overlaySegment.size = size;
        overlaySegment.fileOffset = fileOffset;
        overlaySegment.data.assign(data.begin() + static_cast<std::ptrdiff_t>(fileOffset),
                                   data.begin() + static_cast<std::ptrdiff_t>(endOffset));
        segments.push_back(std::move(overlaySegment));
    }

    return true;
}

void extractSyscalls(const std::vector<PsxExeImage::Segment>& segments,
                     const std::vector<u8>& programData,
                     std::vector<PsxExeImage::SyscallMetadata>& syscalls)
{
    for (const auto& segment : segments)
    {
        const std::vector<u8>* data = &segment.data;
        size_t baseOffset = 0;
        if (data->empty())
        {
            if (segment.fileOffset < PsxExeLoader::kHeaderSize)
            {
                continue;
            }
            baseOffset = static_cast<size_t>(segment.fileOffset - PsxExeLoader::kHeaderSize);
            if (baseOffset + static_cast<size_t>(segment.size) > programData.size())
            {
                continue;
            }
            data = &programData;
        }

        for (u32 offset = 0; offset + sizeof(u32) <= segment.size; offset += sizeof(u32))
        {
            const u32 word =
                readLe32At(*data, baseOffset + static_cast<size_t>(offset)) & 0xFFFFFFFFu;
            if ((word & 0xFC00003Fu) == 0x0000000Cu)
            {
                const u32 code = (word >> 6) & 0xFFFFFu;
                syscalls.push_back({segment.loadAddress + offset, code});
            }
        }
    }
}

void buildDefaultSymbols(const PsxExeImage& image, std::vector<PsxExeImage::Symbol>& symbols)
{
    if (image.entryPoint.pc != 0)
    {
        symbols.push_back({"entry_point", image.entryPoint.pc});
    }
    if (image.entryPoint.gp != 0)
    {
        symbols.push_back({"global_pointer", image.entryPoint.gp});
    }
    if (image.entryPoint.sp != 0)
    {
        symbols.push_back({"stack_pointer", image.entryPoint.sp});
    }

    for (size_t index = 0; index < image.segments.size(); ++index)
    {
        const auto& segment = image.segments[index];
        symbols.push_back({"segment_" + std::to_string(index), segment.loadAddress});
        if (segment.size != 0)
        {
            symbols.push_back(
                {"segment_" + std::to_string(index) + "_end", segment.loadAddress + segment.size});
        }
    }

    for (const auto& syscall : image.syscalls)
    {
        std::ostringstream stream;
        stream << "syscall_0x" << std::hex << syscall.code << "_0x" << syscall.address;
        symbols.push_back({stream.str(), syscall.address});
    }
}

} // namespace detail
} // namespace iso
} // namespace psxrecomp
