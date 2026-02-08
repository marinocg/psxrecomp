#pragma once

#include "psxrecomp/types.h"
#include <cstddef>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace iso
{
namespace detail
{

u16 readLe16(const u8* data);
u32 readLe32(const u8* data);
std::string trimSpaces(const std::string& value);
std::string toUpper(std::string value);
std::string normalizeIsoName(const std::string& name);
std::vector<std::string> splitPath(const std::string& path);
std::string trim(const std::string& value);
std::string decodeJolietName(const u8* data, size_t length);
std::string baseIsoName(const std::string& name);
int isoVersionNumber(const std::string& name);

} // namespace detail
} // namespace iso
} // namespace psxrecomp
