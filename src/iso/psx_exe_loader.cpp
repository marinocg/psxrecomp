#include "psxrecomp/iso/psx_exe_loader.h"

#include <cstring>
#include <fstream>

namespace psxrecomp
{
namespace iso
{

namespace
{

constexpr size_t kTitleOffset = 0x4C;
constexpr size_t kTitleLength = 60;

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

} // namespace

bool PsxExeLoader::parseHeader(const std::vector<u8>& data, PsxExeHeader& outHeader)
{
    if (data.size() < kHeaderSize)
    {
        return false;
    }

    if (std::memcmp(data.data(), "PS-X EXE", 8) != 0)
    {
        return false;
    }

    outHeader.initialPc = readLe32(data.data() + 0x10);
    outHeader.initialGp = readLe32(data.data() + 0x14);
    outHeader.loadAddress = readLe32(data.data() + 0x18);
    outHeader.loadSize = readLe32(data.data() + 0x1C);
    outHeader.bssAddress = readLe32(data.data() + 0x28);
    outHeader.bssSize = readLe32(data.data() + 0x2C);
    outHeader.stackAddress = readLe32(data.data() + 0x30);
    outHeader.stackSize = readLe32(data.data() + 0x34);

    if (kTitleOffset + kTitleLength <= kHeaderSize)
    {
        std::string title(reinterpret_cast<const char*>(data.data() + kTitleOffset), kTitleLength);
        outHeader.title = trimString(title);
    }
    else
    {
        outHeader.title.clear();
    }

    return true;
}

bool PsxExeLoader::loadImage(const std::vector<u8>& data, PsxExeImage& outImage)
{
    PsxExeHeader header{};
    if (!parseHeader(data, header))
    {
        return false;
    }

    if (data.size() < kHeaderSize)
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
        return false;
    }

    if (!isRangeInRam(header.loadAddress, effectiveLoadSize))
    {
        return false;
    }

    if (header.initialPc != 0 &&
        (!isAddressInRam(header.initialPc) || !isAligned(header.initialPc, 4)))
    {
        return false;
    }

    if (header.initialGp != 0 &&
        (!isAddressInRam(header.initialGp) || !isAligned(header.initialGp, 4)))
    {
        return false;
    }

    if (header.bssSize != 0 && !isRangeInRam(header.bssAddress, header.bssSize))
    {
        return false;
    }

    if (header.bssSize != 0 && !isAligned(header.bssAddress, 4))
    {
        return false;
    }

    if (header.stackSize != 0 && !isRangeInRam(header.stackAddress, header.stackSize))
    {
        return false;
    }

    if (header.stackSize != 0 && !isAligned(header.stackAddress, 4))
    {
        return false;
    }

    outImage.header = header;
    outImage.programData.assign(data.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                                data.begin() +
                                    static_cast<std::ptrdiff_t>(kHeaderSize + effectiveLoadSize));
    return true;
}

bool PsxExeLoader::loadFromFile(const std::string& filename, PsxExeImage& outImage)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return false;
    }

    std::streamsize size = file.tellg();
    if (size <= 0)
    {
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<u8> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        return false;
    }

    return loadImage(buffer, outImage);
}

} // namespace iso
} // namespace psxrecomp
