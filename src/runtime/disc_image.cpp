#include "psxrecomp/runtime/disc_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ios>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u64 USER_SECTOR_SIZE = 2048;
constexpr u64 RAW_SECTOR_SIZE = 2352;
constexpr u64 RAW_USER_OFFSET = 24;

std::string toLowerExt(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

DiscImage::Layout detectLayout(const std::filesystem::path& path, u64 bytes)
{
    const bool fits2048 = (bytes % USER_SECTOR_SIZE) == 0;
    const bool fits2352 = (bytes % RAW_SECTOR_SIZE) == 0;
    if (fits2048 && !fits2352)
    {
        return DiscImage::Layout::User2048;
    }
    if (!fits2048 && fits2352)
    {
        return DiscImage::Layout::Raw2352;
    }
    if (fits2048 && fits2352)
    {
        const std::string ext = toLowerExt(path);
        if (ext == ".bin" || ext == ".img")
        {
            return DiscImage::Layout::Raw2352;
        }
        return DiscImage::Layout::User2048;
    }
    return DiscImage::Layout::Auto;
}
} // namespace

bool DiscImage::open(const std::filesystem::path& path, Layout layout)
{
    close();

    std::error_code ec;
    const u64 bytes = static_cast<u64>(std::filesystem::file_size(path, ec));
    if (ec || bytes == 0)
    {
        return false;
    }

    Layout resolved = layout;
    if (resolved == Layout::Auto)
    {
        resolved = detectLayout(path, bytes);
    }
    if (resolved != Layout::User2048 && resolved != Layout::Raw2352)
    {
        return false;
    }

    const u64 stride = resolved == Layout::Raw2352 ? RAW_SECTOR_SIZE : USER_SECTOR_SIZE;
    if ((bytes % stride) != 0)
    {
        return false;
    }

    m_stream.open(path, std::ios::binary);
    if (!m_stream.is_open())
    {
        return false;
    }

    m_layout = resolved;
    m_sectorCount = bytes / stride;
    return true;
}

void DiscImage::close()
{
    if (m_stream.is_open())
    {
        m_stream.close();
    }
    m_layout = Layout::Auto;
    m_sectorCount = 0;
}

bool DiscImage::isOpen() const
{
    return m_stream.is_open();
}

u32 DiscImage::sectorCount() const
{
    return static_cast<u32>(std::min<u64>(m_sectorCount, 0xFFFFFFFFu));
}

u32 DiscImage::userSectorCount() const
{
    return sectorCount();
}

bool DiscImage::readUserSector(u32 lba, std::span<u8, 2048> out)
{
    if (!isOpen() || static_cast<u64>(lba) >= m_sectorCount)
    {
        return false;
    }

    std::streamoff offset = 0;
    if (m_layout == Layout::Raw2352)
    {
        offset =
            static_cast<std::streamoff>(static_cast<u64>(lba) * RAW_SECTOR_SIZE + RAW_USER_OFFSET);
    }
    else
    {
        offset = static_cast<std::streamoff>(static_cast<u64>(lba) * USER_SECTOR_SIZE);
    }

    m_stream.clear();
    m_stream.seekg(offset, std::ios::beg);
    if (!m_stream.good())
    {
        return false;
    }

    m_stream.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return m_stream.good();
}

bool DiscImage::readRawSector2352(u32 lba, std::span<u8, 2352> out)
{
    if (!isOpen() || static_cast<u64>(lba) >= m_sectorCount)
    {
        return false;
    }

    if (m_layout == Layout::Raw2352)
    {
        const std::streamoff offset =
            static_cast<std::streamoff>(static_cast<u64>(lba) * RAW_SECTOR_SIZE);
        m_stream.clear();
        m_stream.seekg(offset, std::ios::beg);
        if (!m_stream.good())
        {
            return false;
        }
        m_stream.read(reinterpret_cast<char*>(out.data()),
                      static_cast<std::streamsize>(out.size()));
        return m_stream.good();
    }

    std::array<u8, 2048> userBytes{};
    if (!readUserSector(lba, std::span<u8, 2048>(userBytes)))
    {
        return false;
    }
    std::fill(out.begin(), out.end(), 0);
    std::copy(userBytes.begin(), userBytes.end(),
              out.begin() + static_cast<std::ptrdiff_t>(RAW_USER_OFFSET));
    return true;
}

} // namespace runtime
} // namespace psxrecomp
