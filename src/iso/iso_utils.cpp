#include "iso_utils.h"

#include <algorithm>
#include <cctype>

namespace psxrecomp
{
namespace iso
{
namespace detail
{

u16 readLe16(const u8* data)
{
    return static_cast<u16>(data[0]) | (static_cast<u16>(data[1]) << 8);
}

u32 readLe32(const u8* data)
{
    return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
           (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
}

std::string trimSpaces(const std::string& value)
{
    auto start = value.find_first_not_of(' ');
    if (start == std::string::npos)
    {
        return "";
    }
    auto end = value.find_last_not_of(' ');
    return value.substr(start, end - start + 1);
}

std::string toUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string normalizeIsoName(const std::string& name)
{
    auto upper = toUpper(name);
    auto semicolon = upper.find(';');
    if (semicolon != std::string::npos)
    {
        upper.erase(semicolon);
    }
    return upper;
}

std::vector<std::string> splitPath(const std::string& path)
{
    std::vector<std::string> parts;
    std::string current;
    for (char ch : path)
    {
        if (ch == '/' || ch == '\\')
        {
            if (!current.empty())
            {
                parts.push_back(current);
                current.clear();
            }
        }
        else
        {
            current.push_back(ch);
        }
    }
    if (!current.empty())
    {
        parts.push_back(current);
    }
    return parts;
}

std::string trim(const std::string& value)
{
    auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
        return "";
    }
    auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string decodeJolietName(const u8* data, size_t length)
{
    std::string result;
    result.reserve((length / 2) * 3);
    for (size_t i = 0; i + 1 < length; i += 2)
    {
        u16 code = static_cast<u16>(data[i] << 8) | static_cast<u16>(data[i + 1]);
        if (code == 0)
        {
            continue;
        }
        if (code <= 0x7F)
        {
            result.push_back(static_cast<char>(code));
        }
        else if (code <= 0x07FF)
        {
            char b1 = static_cast<char>(0xC0 | ((code >> 6) & 0x1F));
            char b2 = static_cast<char>(0x80 | (code & 0x3F));
            result.push_back(b1);
            result.push_back(b2);
        }
        else
        {
            if (code >= 0xD800 && code <= 0xDFFF)
            {
                result.push_back('?');
                continue;
            }
            char b1 = static_cast<char>(0xE0 | ((code >> 12) & 0x0F));
            char b2 = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            char b3 = static_cast<char>(0x80 | (code & 0x3F));
            result.push_back(b1);
            result.push_back(b2);
            result.push_back(b3);
        }
    }
    return result;
}

std::string baseIsoName(const std::string& name)
{
    auto semicolon = name.find(';');
    if (semicolon != std::string::npos)
    {
        return name.substr(0, semicolon);
    }
    return name;
}

int isoVersionNumber(const std::string& name)
{
    auto semicolon = name.find(';');
    if (semicolon == std::string::npos)
    {
        return 0;
    }
    int value = 0;
    for (size_t i = semicolon + 1; i < name.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(name[i])))
        {
            break;
        }
        value = (value * 10) + (name[i] - '0');
    }
    return value;
}

} // namespace detail
} // namespace iso
} // namespace psxrecomp
