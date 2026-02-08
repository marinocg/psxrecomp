#include "iso_boot.h"

#include "iso_utils.h"

#include <algorithm>
#include <sstream>

namespace psxrecomp
{
namespace iso
{
namespace detail
{

std::string parseBootPathFromSystemCnf(const std::vector<u8>& systemCnf)
{
    if (systemCnf.empty())
    {
        return "";
    }

    std::string contents(systemCnf.begin(), systemCnf.end());
    std::istringstream stream(contents);
    std::string line;
    while (std::getline(stream, line))
    {
        auto upper = detail::toUpper(line);
        auto pos = upper.find("BOOT");
        if (pos == std::string::npos)
        {
            continue;
        }
        auto equals = upper.find('=', pos);
        if (equals == std::string::npos)
        {
            continue;
        }
        std::string value = line.substr(equals + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        auto upperValue = detail::toUpper(value);
        auto prefixPos = upperValue.find("CDROM");
        if (prefixPos != std::string::npos)
        {
            auto colonPos = upperValue.find(':', prefixPos);
            if (colonPos != std::string::npos)
            {
                value = value.substr(colonPos + 1);
            }
        }
        while (!value.empty() && (value[0] == '\\' || value[0] == '/'))
        {
            value.erase(value.begin());
        }
        std::replace(value.begin(), value.end(), '\\', '/');
        return detail::normalizeIsoName(value);
    }

    return "";
}

} // namespace detail
} // namespace iso
} // namespace psxrecomp
