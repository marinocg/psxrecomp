#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class ResourcePack
{
  public:
    bool loadFromDirectory(const std::filesystem::path& root);
    bool hasResource(const std::string& logicalPath) const;
    std::optional<std::vector<uint8_t>> readResource(const std::string& logicalPath) const;
    size_t resourceCount() const;

  private:
    std::unordered_map<std::string, std::filesystem::path> m_index;
};

} // namespace runtime
} // namespace psxrecomp
