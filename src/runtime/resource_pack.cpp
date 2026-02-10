#include "psxrecomp/runtime/resource_pack.h"

#include <fstream>
#include <limits>
#include <system_error>

namespace psxrecomp
{
namespace runtime
{

bool ResourcePack::loadFromDirectory(const std::filesystem::path& root)
{
    m_index.clear();

    std::error_code error;
    if (!std::filesystem::exists(root, error) || error)
    {
        return false;
    }
    if (!std::filesystem::is_directory(root, error) || error)
    {
        return false;
    }

    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, error);
    if (error)
    {
        return false;
    }

    const auto end = std::filesystem::recursive_directory_iterator();
    while (iterator != end)
    {
        const auto& entry = *iterator;

        if (entry.is_regular_file(error) && !error)
        {
            auto relative = std::filesystem::relative(entry.path(), root, error);
            if (!error)
            {
                m_index[relative.generic_string()] = entry.path();
            }
        }
        error.clear();

        iterator.increment(error);
        if (error)
        {
            error.clear();
        }
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
    if (!file)
    {
        return std::nullopt;
    }

    auto size = file.tellg();
    if (size < 0)
    {
        return std::nullopt;
    }

    auto fileSize = static_cast<std::streamoff>(size);
    if (fileSize < 0 || fileSize > std::numeric_limits<std::streamsize>::max() ||
        static_cast<uint64_t>(fileSize) > std::numeric_limits<size_t>::max())
    {
        return std::nullopt;
    }

    file.seekg(0, std::ios::beg);
    if (!file)
    {
        return std::nullopt;
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
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
