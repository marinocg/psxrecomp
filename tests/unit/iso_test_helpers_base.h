#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace iso_test
{

constexpr uint32_t kSectorSize = 2048;
constexpr uint32_t kRawSectorSize = 2352;
constexpr uint32_t kMode2DataOffset = 24;

inline uint32_t generateUniqueSuffix()
{
    static std::random_device randomDevice;
    static std::mt19937 generator(randomDevice());
    static std::uniform_int_distribution<uint32_t> distribution;
    return distribution(generator);
}

inline void writeLe16(std::vector<uint8_t>& buffer, size_t offset, uint16_t value)
{
    buffer[offset] = static_cast<uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

inline void writeLe32(std::vector<uint8_t>& buffer, size_t offset, uint32_t value)
{
    buffer[offset] = static_cast<uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

inline size_t writeDirectoryRecord(std::vector<uint8_t>& buffer, size_t offset,
                                   const std::string& name, uint32_t extent, uint32_t size,
                                   uint8_t flags)
{
    uint8_t nameLength = static_cast<uint8_t>(name.size());
    uint8_t recordLength = static_cast<uint8_t>(33 + nameLength + (nameLength % 2 == 0 ? 1 : 0));
    buffer[offset] = recordLength;
    buffer[offset + 1] = 0;
    writeLe32(buffer, offset + 2, extent);
    writeLe32(buffer, offset + 10, size);
    buffer[offset + 25] = flags;
    buffer[offset + 26] = 0;
    buffer[offset + 27] = 0;
    writeLe16(buffer, offset + 28, 1);
    buffer[offset + 32] = nameLength;
    std::memcpy(buffer.data() + offset + 33, name.data(), nameLength);
    return recordLength;
}

inline size_t writeJolietDirectoryRecord(std::vector<uint8_t>& buffer, size_t offset,
                                         const std::u16string& name, uint32_t extent, uint32_t size,
                                         uint8_t flags)
{
    uint8_t nameLength = static_cast<uint8_t>(name.size() * 2);
    uint8_t recordLength = static_cast<uint8_t>(33 + nameLength + (nameLength % 2 == 0 ? 1 : 0));
    buffer[offset] = recordLength;
    buffer[offset + 1] = 0;
    writeLe32(buffer, offset + 2, extent);
    writeLe32(buffer, offset + 10, size);
    buffer[offset + 25] = flags;
    buffer[offset + 26] = 0;
    buffer[offset + 27] = 0;
    writeLe16(buffer, offset + 28, 1);
    buffer[offset + 32] = nameLength;
    for (size_t i = 0; i < name.size(); ++i)
    {
        buffer[offset + 33 + (i * 2)] = static_cast<uint8_t>((name[i] >> 8) & 0xFF);
        buffer[offset + 33 + (i * 2) + 1] = static_cast<uint8_t>(name[i] & 0xFF);
    }
    return recordLength;
}

inline size_t writePathTableEntry(std::vector<uint8_t>& buffer, size_t offset,
                                  const std::string& name, uint32_t extent, uint16_t parent)
{
    uint8_t nameLength = static_cast<uint8_t>(name.size());
    buffer[offset] = nameLength;
    buffer[offset + 1] = 0;
    writeLe32(buffer, offset + 2, extent);
    writeLe16(buffer, offset + 6, parent);
    if (nameLength > 0)
    {
        std::memcpy(buffer.data() + offset + 8, name.data(), nameLength);
    }
    offset += 8 + nameLength;
    if (nameLength % 2 == 1)
    {
        buffer[offset] = 0;
        ++offset;
    }
    return offset;
}

inline void writeMode2Sector(std::vector<uint8_t>& image, uint32_t lba,
                             const std::vector<uint8_t>& data, uint8_t submode)
{
    size_t offset = static_cast<size_t>(lba) * kRawSectorSize;
    image[offset + 15] = 2;
    image[offset + 18] = submode;
    size_t copySize = (submode & 0x20) != 0 ? 2324 : 2048;
    std::memcpy(image.data() + offset + kMode2DataOffset, data.data(),
                std::min(copySize, data.size()));
}

inline void writeMode2FormSector(std::vector<uint8_t>& image, uint32_t lba,
                                 const std::vector<uint8_t>& data, bool form2)
{
    writeMode2Sector(image, lba, data, form2 ? static_cast<uint8_t>(0x20) : 0x00);
}

inline void writeXaAudioSector(std::vector<uint8_t>& image, uint32_t lba,
                               const std::vector<uint8_t>& data)
{
    writeMode2Sector(image, lba, data, 0x24);
    size_t offset = static_cast<size_t>(lba) * kRawSectorSize;
    constexpr uint8_t kFileNumber = 1;
    constexpr uint8_t kChannelNumber = 1;
    constexpr uint8_t kSubmode = 0x24;
    constexpr uint8_t kCodingInfo = 0;
    image[offset + 16] = kFileNumber;
    image[offset + 17] = kChannelNumber;
    image[offset + 18] = kSubmode;
    image[offset + 19] = kCodingInfo;
    image[offset + 20] = kFileNumber;
    image[offset + 21] = kChannelNumber;
    image[offset + 22] = kSubmode;
    image[offset + 23] = kCodingInfo;
}

} // namespace iso_test
