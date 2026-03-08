#pragma once

// Private implementation details shared across the GTE split translation units.
// Everything here is either constexpr or static inline so each including TU
// gets its own self-contained copy without external-linkage conflicts.

#include "psxrecomp/runtime/gte.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace psxrecomp
{
namespace runtime
{
namespace gte_detail
{

static constexpr u8 kDataVxy0 = 0;
static constexpr u8 kDataVz0 = 1;
static constexpr u8 kDataVxy1 = 2;
static constexpr u8 kDataVz1 = 3;
static constexpr u8 kDataVxy2 = 4;
static constexpr u8 kDataVz2 = 5;
static constexpr u8 kDataRgbc = 6;
static constexpr u8 kDataOtz = 7;
static constexpr u8 kDataIr0 = 8;
static constexpr u8 kDataIr1 = 9;
static constexpr u8 kDataIr2 = 10;
static constexpr u8 kDataIr3 = 11;
static constexpr u8 kDataSxy0 = 12;
static constexpr u8 kDataSxy1 = 13;
static constexpr u8 kDataSxy2 = 14;
static constexpr u8 kDataSxyp = 15;
static constexpr u8 kDataSz0 = 16;
static constexpr u8 kDataSz1 = 17;
static constexpr u8 kDataSz2 = 18;
static constexpr u8 kDataSz3 = 19;
static constexpr u8 kDataRgb0 = 20;
static constexpr u8 kDataRgb1 = 21;
static constexpr u8 kDataRgb2 = 22;
static constexpr u8 kDataMac0 = 24;
static constexpr u8 kDataMac1 = 25;
static constexpr u8 kDataMac2 = 26;
static constexpr u8 kDataMac3 = 27;
static constexpr u8 kDataIrgb = 28;
static constexpr u8 kDataOrgb = 29;
static constexpr u8 kDataLzcs = 30;
static constexpr u8 kDataLzcr = 31;

static constexpr u8 kCtrlTrx = 5;
static constexpr u8 kCtrlTry = 6;
static constexpr u8 kCtrlTrz = 7;
static constexpr u8 kCtrlRbk = 13;
static constexpr u8 kCtrlGbk = 14;
static constexpr u8 kCtrlBbk = 15;
static constexpr u8 kCtrlRfc = 21;
static constexpr u8 kCtrlGfc = 22;
static constexpr u8 kCtrlBfc = 23;
static constexpr u8 kCtrlOfx = 24;
static constexpr u8 kCtrlOfy = 25;
static constexpr u8 kCtrlH = 26;
static constexpr u8 kCtrlDqa = 27;
static constexpr u8 kCtrlDqb = 28;
static constexpr u8 kCtrlZsf3 = 29;
static constexpr u8 kCtrlZsf4 = 30;
static constexpr u8 kCtrlFlag = 31;

static constexpr u32 kFlagMac1Positive = 30;
static constexpr u32 kFlagMac2Positive = 29;
static constexpr u32 kFlagMac3Positive = 28;
static constexpr u32 kFlagMac1Negative = 27;
static constexpr u32 kFlagMac2Negative = 26;
static constexpr u32 kFlagMac3Negative = 25;
static constexpr u32 kFlagIr1Saturated = 24;
static constexpr u32 kFlagIr2Saturated = 23;
static constexpr u32 kFlagIr3Saturated = 22;
static constexpr u32 kFlagColorRSaturated = 21;
static constexpr u32 kFlagColorGSaturated = 20;
static constexpr u32 kFlagColorBSaturated = 19;
static constexpr u32 kFlagDepthSaturated = 18;
static constexpr u32 kFlagDivideOverflow = 17;
static constexpr u32 kFlagMac0Positive = 16;
static constexpr u32 kFlagMac0Negative = 15;
static constexpr u32 kFlagSx2Saturated = 14;
static constexpr u32 kFlagSy2Saturated = 13;
static constexpr u32 kFlagIr0Saturated = 12;
static constexpr u32 kFlagErrorMask = 0x7F87E000u;

static inline bool traceGteEnabled()
{
    static const bool enabled = []()
    {
        if (const char* env = std::getenv("PSXRECOMP_TRACE_GTE"))
        {
            return env[0] == '1';
        }
        return false;
    }();
    return enabled;
}

static inline void traceGte(const char* fmt, ...)
{
    if (!traceGteEnabled())
    {
        return;
    }

    std::fputs("[gte] ", stderr);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}

static constexpr s32 signExtend16(u32 value)
{
    return static_cast<s32>(static_cast<int16_t>(value & 0xFFFFu));
}

static constexpr s64 signExtend32(u32 value)
{
    return static_cast<s64>(static_cast<s32>(value));
}

static constexpr u32 zeroExtend16(u32 value)
{
    return value & 0xFFFFu;
}

static constexpr s16 lowHalfSigned(u32 value)
{
    return static_cast<s16>(value & 0xFFFFu);
}

static constexpr s16 highHalfSigned(u32 value)
{
    return static_cast<s16>((value >> 16) & 0xFFFFu);
}

static inline u32 packHalfWords(s16 lo, s16 hi)
{
    return static_cast<u32>(static_cast<u16>(lo)) | (static_cast<u32>(static_cast<u16>(hi)) << 16);
}

static inline u32 packRgbc(u8 r, u8 g, u8 b, u8 code)
{
    return static_cast<u32>(r) | (static_cast<u32>(g) << 8) | (static_cast<u32>(b) << 16) |
           (static_cast<u32>(code) << 24);
}

static inline u32 packIrgb(s32 ir1, s32 ir2, s32 ir3)
{
    auto pack5 = [](s32 value) -> u32
    {
        if (value <= 0)
        {
            return 0;
        }
        if (value >= 0x7FFF)
        {
            return 0x1Fu;
        }
        return static_cast<u32>(value >> 7) & 0x1Fu;
    };
    return pack5(ir1) | (pack5(ir2) << 5) | (pack5(ir3) << 10);
}

static inline unsigned countLeadingMatchingBits(u32 value)
{
    const bool signBit = (value & 0x80000000u) != 0u;
    unsigned count = 0;
    for (unsigned bit = 0; bit < 32; ++bit)
    {
        const bool current = (value & (0x80000000u >> bit)) != 0u;
        if (current != signBit)
        {
            break;
        }
        ++count;
    }
    return count;
}

static inline s64 arithmeticShiftRight(s64 value, unsigned shift)
{
    if (shift == 0)
    {
        return value;
    }
    if (value >= 0)
    {
        return value >> shift;
    }
    return -(((-value) + ((1LL << shift) - 1)) >> shift);
}

static inline void appendU32(std::vector<u8>& out, u32 value)
{
    out.push_back(static_cast<u8>(value & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
}

static inline bool consumeU32(const std::vector<u8>& data, size_t& cursor, u32& out)
{
    if (cursor + sizeof(u32) > data.size())
    {
        return false;
    }

    out = static_cast<u32>(data[cursor]) | (static_cast<u32>(data[cursor + 1]) << 8) |
          (static_cast<u32>(data[cursor + 2]) << 16) | (static_cast<u32>(data[cursor + 3]) << 24);
    cursor += sizeof(u32);
    return true;
}

} // namespace gte_detail
} // namespace runtime
} // namespace psxrecomp
