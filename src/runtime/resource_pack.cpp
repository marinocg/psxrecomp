#include "psxrecomp/runtime/resource_pack.h"

#include <fstream>

namespace psxrecomp
{
namespace runtime
{

bool ResourcePack::loadFromDirectory(const std::filesystem::path& root)
{
    m_index.clear();
    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root))
    {
        return false;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        auto relative = std::filesystem::relative(entry.path(), root).generic_string();
        m_index[relative] = entry.path();
    }

    return true;
}

bool ResourcePack::hasResource(const std::string& logicalPath) const
{
    return m_index.find(logicalPath) != m_index.end();
}

std::optional<std::vector<uint8_t>> ResourcePack::readResource(const std::string& logicalPath) const
{
    auto it = m_index.find(logicalPath);
    if (it == m_index.end())
    {
        return std::nullopt;
    }

    std::ifstream file(it->second, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    if (size < 0)
    {
        return std::nullopt;
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!buffer.empty())
    {
        file.read(reinterpret_cast<char*>(buffer.data()),
                  static_cast<std::streamsize>(buffer.size()));
        if (!file)
        {
            return std::nullopt;
        }
    }

    return buffer;
}

size_t ResourcePack::resourceCount() const
{
    return m_index.size();
}

} // namespace runtime
} // namespace psxrecomp
