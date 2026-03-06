#pragma once

#include "psxrecomp/runtime/disc.h"

#include <filesystem>
#include <fstream>

namespace psxrecomp
{
namespace runtime
{

class DiscImage final : public Disc
{
  public:
    enum class Layout
    {
        Auto,
        User2048,
        Raw2352
    };

    bool open(const std::filesystem::path& path, Layout layout = Layout::Auto);
    void close();
    bool isOpen() const;
    u32 sectorCount() const;
    u32 userSectorCount() const override;

    bool readUserSector(u32 lba, std::span<u8, 2048> out) override;
    bool readRawSector2352(u32 lba, std::span<u8, 2352> out) override;

  private:
    std::ifstream m_stream;
    Layout m_layout = Layout::Auto;
    u64 m_sectorCount = 0;
};

} // namespace runtime
} // namespace psxrecomp
