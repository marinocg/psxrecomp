#pragma once

#include "psxrecomp/runtime/psx_system.h"

namespace psxrecomp
{
namespace runtime
{

inline u32 mergeLoadWordLeft(u32 memoryWord, u32 registerValue, u32 byteOffset)
{
    switch (byteOffset & 0x3u)
    {
    case 0:
        return (registerValue & 0x00FFFFFFu) | (memoryWord << 24);
    case 1:
        return (registerValue & 0x0000FFFFu) | (memoryWord << 16);
    case 2:
        return (registerValue & 0x000000FFu) | (memoryWord << 8);
    case 3:
        return memoryWord;
    default:
        return registerValue;
    }
}

inline u32 mergeLoadWordRight(u32 memoryWord, u32 registerValue, u32 byteOffset)
{
    switch (byteOffset & 0x3u)
    {
    case 0:
        return memoryWord;
    case 1:
        return (registerValue & 0xFF000000u) | (memoryWord >> 8);
    case 2:
        return (registerValue & 0xFFFF0000u) | (memoryWord >> 16);
    case 3:
        return (registerValue & 0xFFFFFF00u) | (memoryWord >> 24);
    default:
        return registerValue;
    }
}

inline u32 mergeStoreWordLeft(u32 memoryWord, u32 registerValue, u32 byteOffset)
{
    switch (byteOffset & 0x3u)
    {
    case 0:
        return (memoryWord & 0xFFFFFF00u) | (registerValue >> 24);
    case 1:
        return (memoryWord & 0xFFFF0000u) | (registerValue >> 16);
    case 2:
        return (memoryWord & 0xFF000000u) | (registerValue >> 8);
    case 3:
        return registerValue;
    default:
        return memoryWord;
    }
}

inline u32 mergeStoreWordRight(u32 memoryWord, u32 registerValue, u32 byteOffset)
{
    switch (byteOffset & 0x3u)
    {
    case 0:
        return registerValue;
    case 1:
        return (memoryWord & 0x000000FFu) | (registerValue << 8);
    case 2:
        return (memoryWord & 0x0000FFFFu) | (registerValue << 16);
    case 3:
        return (memoryWord & 0x00FFFFFFu) | (registerValue << 24);
    default:
        return memoryWord;
    }
}

inline u32 loadWordLeft(PsxSystem& system, Address address, u32 registerValue)
{
    const Address alignedAddress = address & ~0x3u;
    const u32 memoryWord = system.read<u32>(alignedAddress);
    return mergeLoadWordLeft(memoryWord, registerValue, address & 0x3u);
}

inline u32 loadWordRight(PsxSystem& system, Address address, u32 registerValue)
{
    const Address alignedAddress = address & ~0x3u;
    const u32 memoryWord = system.read<u32>(alignedAddress);
    return mergeLoadWordRight(memoryWord, registerValue, address & 0x3u);
}

inline void storeWordLeft(PsxSystem& system, Address address, u32 registerValue)
{
    const Address alignedAddress = address & ~0x3u;
    const u32 memoryWord = system.read<u32>(alignedAddress);
    system.write<u32>(alignedAddress,
                      mergeStoreWordLeft(memoryWord, registerValue, address & 0x3u));
}

inline void storeWordRight(PsxSystem& system, Address address, u32 registerValue)
{
    const Address alignedAddress = address & ~0x3u;
    const u32 memoryWord = system.read<u32>(alignedAddress);
    system.write<u32>(alignedAddress,
                      mergeStoreWordRight(memoryWord, registerValue, address & 0x3u));
}

} // namespace runtime
} // namespace psxrecomp
