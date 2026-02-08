#pragma once

#include "psxrecomp/types.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace psxrecomp
{
namespace iso
{

class PathTable
{
  public:
    bool parse(const std::vector<u8>& data, std::string& errorMessage);
    bool empty() const;
    std::optional<u32> findExtent(const std::string& normalizedPath) const;
    const std::unordered_map<std::string, u32>& getPathMap() const;

  private:
    std::unordered_map<std::string, u32> m_pathToExtent;
};

} // namespace iso
} // namespace psxrecomp
