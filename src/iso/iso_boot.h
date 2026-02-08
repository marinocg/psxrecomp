#pragma once

#include "psxrecomp/types.h"
#include <string>
#include <vector>

namespace psxrecomp
{
namespace iso
{
namespace detail
{

std::string parseBootPathFromSystemCnf(const std::vector<u8>& systemCnf);

} // namespace detail
} // namespace iso
} // namespace psxrecomp
