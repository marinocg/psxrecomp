#pragma once

#include "psxrecomp/types.h"

#include <cstddef>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

class RuntimeLogger;

/**
 * @brief Shared BIOS helper: resolve a RAM pointer from a PSX address.
 */
inline u8* ramPointer(u8* ram, u32 address)
{
    return ram + (address & 0x1FFFFF);
}

/**
 * @brief Shared BIOS helper: resolve a const RAM pointer from a PSX address.
 */
inline const u8* ramPointerConst(const u8* ram, u32 address)
{
    return ram + (address & 0x1FFFFF);
}

/**
 * @brief Build a BIOS vector name string.
 */
inline const char* vectorName(u32 vector)
{
    if (vector == 0xA0)
    {
        return "A0";
    }
    if (vector == 0xB0)
    {
        return "B0";
    }
    if (vector == 0xC0)
    {
        return "C0";
    }
    return "??";
}

} // namespace runtime
} // namespace psxrecomp
