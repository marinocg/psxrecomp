#include "path_table.h"

#include "iso_utils.h"

namespace psxrecomp
{
namespace iso
{

bool PathTable::parse(const std::vector<u8>& data, std::string& errorMessage)
{
    m_pathToExtent.clear();
    errorMessage.clear();

    struct Entry
    {
        std::string name;
        u32 extent = 0;
        u16 parentIndex = 0;
    };

    std::vector<Entry> entries;
    entries.reserve(32);

    size_t offset = 0;
    while (offset + 8 <= data.size())
    {
        u8 nameLength = data[offset];
        u8 extendedLength = data[offset + 1];
        (void)extendedLength;
        u32 extent = detail::readLe32(data.data() + offset + 2);
        u16 parentIndex = detail::readLe16(data.data() + offset + 6);
        offset += 8;
        if (offset + nameLength > data.size())
        {
            errorMessage = "Path table entry exceeds buffer size.";
            return false;
        }
        std::string name;
        if (nameLength > 0)
        {
            name.assign(reinterpret_cast<const char*>(data.data() + offset), nameLength);
        }
        offset += nameLength;
        if (nameLength % 2 == 1)
        {
            if (offset >= data.size())
            {
                errorMessage = "Path table entry padding out of range.";
                return false;
            }
            ++offset;
        }

        Entry entry{};
        entry.name = name;
        entry.extent = extent;
        entry.parentIndex = parentIndex;
        entries.push_back(entry);
    }

    if (entries.empty())
    {
        errorMessage = "Path table is empty.";
        return false;
    }

    std::vector<std::string> paths(entries.size() + 1);
    paths[0] = "";
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries[i];
        std::string normalizedName;
        if (entry.name.size() == 1 && entry.name[0] == '\0')
        {
            normalizedName = "";
        }
        else
        {
            normalizedName = detail::normalizeIsoName(entry.name);
        }
        if (entry.parentIndex == 0 || entry.parentIndex > i + 1 ||
            entry.parentIndex >= paths.size())
        {
            errorMessage = "Invalid parent index in path table.";
            return false;
        }
        std::string parentPath = paths[entry.parentIndex];
        std::string fullPath = parentPath;
        if (!normalizedName.empty())
        {
            if (!fullPath.empty())
            {
                fullPath += "/";
            }
            fullPath += normalizedName;
        }
        paths[i + 1] = fullPath;
        m_pathToExtent[fullPath] = entry.extent;
    }

    return true;
}

bool PathTable::empty() const
{
    return m_pathToExtent.empty();
}

std::optional<u32> PathTable::findExtent(const std::string& normalizedPath) const
{
    auto it = m_pathToExtent.find(normalizedPath);
    if (it == m_pathToExtent.end())
    {
        return std::nullopt;
    }
    return it->second;
}

const std::unordered_map<std::string, u32>& PathTable::getPathMap() const
{
    return m_pathToExtent;
}

} // namespace iso
} // namespace psxrecomp
