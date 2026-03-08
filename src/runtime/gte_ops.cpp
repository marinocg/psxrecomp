#include "psxrecomp/runtime/gte.h"

#include "gte_internal.h"

namespace psxrecomp
{
namespace runtime
{

using namespace gte_detail;

void Gte::finalizeCommandFlags()
{
    updateFlagSummaryBit();
}

void Gte::setFlag(u32 bit)
{
    m_ctrlRegs[kCtrlFlag] |= (1u << bit);
}

void Gte::setMac0(s64 value)
{
    if (value > std::numeric_limits<int32_t>::max())
    {
        setFlag(kFlagMac0Positive);
    }
    else if (value < std::numeric_limits<int32_t>::min())
    {
        setFlag(kFlagMac0Negative);
    }
    m_dataRegs[kDataMac0] = static_cast<u32>(static_cast<s32>(value));
}

void Gte::setMac(u8 component, s64 value)
{
    static constexpr std::array<u32, 3> kPositiveBits = {kFlagMac1Positive, kFlagMac2Positive,
                                                         kFlagMac3Positive};
    static constexpr std::array<u32, 3> kNegativeBits = {kFlagMac1Negative, kFlagMac2Negative,
                                                         kFlagMac3Negative};
    static constexpr std::array<u8, 3> kMacRegs = {kDataMac1, kDataMac2, kDataMac3};
    const s64 maxValue = (1LL << 43) - 1;
    const s64 minValue = -(1LL << 43);

    if (component == 0 || component > 3)
    {
        return;
    }

    const size_t index = static_cast<size_t>(component - 1);
    if (value > maxValue)
    {
        setFlag(kPositiveBits[index]);
    }
    else if (value < minValue)
    {
        setFlag(kNegativeBits[index]);
    }
    m_dataRegs[kMacRegs[index]] = static_cast<u32>(static_cast<s32>(value));
}

void Gte::setMacVector(const ColorVec& values)
{
    for (u8 component = 0; component < 3; ++component)
    {
        setMac(static_cast<u8>(component + 1), values[component]);
    }
}

s16 Gte::setIr(u8 component, s64 value, bool lm)
{
    static constexpr std::array<u32, 3> kSaturationBits = {kFlagIr1Saturated, kFlagIr2Saturated,
                                                           kFlagIr3Saturated};
    static constexpr std::array<u8, 3> kIrRegs = {kDataIr1, kDataIr2, kDataIr3};

    if (component == 0 || component > 3)
    {
        return 0;
    }

    const size_t index = static_cast<size_t>(component - 1);
    const s64 minValue = lm ? 0 : -0x8000;
    s64 clamped = value;
    if (clamped > 0x7FFF)
    {
        clamped = 0x7FFF;
        setFlag(kSaturationBits[index]);
    }
    else if (clamped < minValue)
    {
        clamped = minValue;
        setFlag(kSaturationBits[index]);
    }

    m_dataRegs[kIrRegs[index]] = static_cast<u32>(static_cast<s32>(clamped));
    m_pendingIrgbPacked = 0;
    m_pendingIrgbIr12CyclesRemaining = 0;
    m_pendingIrgbIr3CyclesRemaining = 0;
    m_irgbReadBusyCyclesRemaining = 0;
    m_pendingIrgbWriteValid = false;
    return static_cast<s16>(clamped);
}

void Gte::setIrVector(const ColorVec& values, bool lm)
{
    for (u8 component = 0; component < 3; ++component)
    {
        setIr(static_cast<u8>(component + 1), values[component], lm);
    }
}

u16 Gte::setIr0(s64 value)
{
    s64 clamped = value;
    if (clamped > 0x1000)
    {
        clamped = 0x1000;
        setFlag(kFlagIr0Saturated);
    }
    else if (clamped < 0)
    {
        clamped = 0;
        setFlag(kFlagIr0Saturated);
    }
    m_dataRegs[kDataIr0] = static_cast<u32>(clamped);
    return static_cast<u16>(clamped);
}

void Gte::pushScreenXy(s16 sx, s16 sy)
{
    m_dataRegs[kDataSxy0] = m_dataRegs[kDataSxy1];
    m_dataRegs[kDataSxy1] = m_dataRegs[kDataSxy2];
    m_dataRegs[kDataSxy2] = packHalfWords(sx, sy);
    m_dataRegs[kDataSxyp] = m_dataRegs[kDataSxy2];

    m_fifo.screenXy[0] = m_dataRegs[kDataSxy0];
    m_fifo.screenXy[1] = m_dataRegs[kDataSxy1];
    m_fifo.screenXy[2] = m_dataRegs[kDataSxy2];
}

void Gte::pushScreenZ(u16 sz)
{
    m_dataRegs[kDataSz0] = m_dataRegs[kDataSz1];
    m_dataRegs[kDataSz1] = m_dataRegs[kDataSz2];
    m_dataRegs[kDataSz2] = m_dataRegs[kDataSz3];
    m_dataRegs[kDataSz3] = sz;

    m_fifo.screenZ[0] = static_cast<u16>(m_dataRegs[kDataSz0] & 0xFFFFu);
    m_fifo.screenZ[1] = static_cast<u16>(m_dataRegs[kDataSz1] & 0xFFFFu);
    m_fifo.screenZ[2] = static_cast<u16>(m_dataRegs[kDataSz2] & 0xFFFFu);
    m_fifo.screenZ[3] = static_cast<u16>(m_dataRegs[kDataSz3] & 0xFFFFu);
}

Gte::ColorVec Gte::multiplyMatrix(const Matrix3x3& matrix, const Vec3& vector,
                                  const Vec3* translation, bool sf, bool farColorBug) const
{
    ColorVec values{};
    const unsigned shift = sf ? 12 : 0;

    for (u8 row = 0; row < 3; ++row)
    {
        s64 base = 0;
        if (farColorBug)
        {
            base = static_cast<s64>(matrix.rows[row][2]) * vector.z;
        }
        else
        {
            base = static_cast<s64>(matrix.rows[row][0]) * vector.x +
                   static_cast<s64>(matrix.rows[row][1]) * vector.y +
                   static_cast<s64>(matrix.rows[row][2]) * vector.z;
            if (translation != nullptr)
            {
                const s32 translationComponent =
                    row == 0 ? translation->x : (row == 1 ? translation->y : translation->z);
                base += static_cast<s64>(translationComponent) * 4096;
            }
        }

        values[row] = arithmeticShiftRight(base, shift);
    }

    return values;
}

Gte::ColorVec Gte::multiplyRgbByIr(const ColorCode& color) const
{
    const Vec3 ir = readIrVector();
    return {static_cast<s64>(color.r) * ir.x * 16, static_cast<s64>(color.g) * ir.y * 16,
            static_cast<s64>(color.b) * ir.z * 16};
}

Gte::ColorVec Gte::interpolateFarColor(const ColorVec& mac, bool sf)
{
    const Vec3 farColor = readTranslationVector(2);
    const u16 ir0 = static_cast<u16>(m_dataRegs[kDataIr0] & 0xFFFFu);
    const unsigned shift = sf ? 12 : 0;
    ColorVec values{};

    for (u8 component = 0; component < 3; ++component)
    {
        const s32 farComponent =
            component == 0 ? farColor.x : (component == 1 ? farColor.y : farColor.z);
        const s64 delta =
            arithmeticShiftRight((static_cast<s64>(farComponent) << 12) - mac[component], shift);
        const s16 ir = setIr(static_cast<u8>(component + 1), delta, false);
        values[component] = mac[component] + static_cast<s64>(ir) * ir0;
    }

    return values;
}

Gte::ColorVec Gte::applyShiftToColorVec(const ColorVec& mac, bool sf) const
{
    if (!sf)
    {
        return mac;
    }

    ColorVec shifted{};
    for (u8 component = 0; component < 3; ++component)
    {
        shifted[component] = arithmeticShiftRight(mac[component], 12);
    }
    return shifted;
}

void Gte::finalizeColorCommand(const ColorVec& values, bool lm, u8 code)
{
    setMacVector(values);
    setIrVector(values, lm);
    pushColorFromMac(code);
}

void Gte::pushColorFromMac(u8 code)
{
    const auto mac = readMacVector();
    const auto saturateColor = [this](s64 value, u32 bit) -> u8
    {
        const s64 color = arithmeticShiftRight(value, 4);
        if (color < 0)
        {
            setFlag(bit);
            return 0;
        }
        if (color > 0xFF)
        {
            setFlag(bit);
            return 0xFF;
        }
        return static_cast<u8>(color);
    };

    const u8 r = saturateColor(mac[0], kFlagColorRSaturated);
    const u8 g = saturateColor(mac[1], kFlagColorGSaturated);
    const u8 b = saturateColor(mac[2], kFlagColorBSaturated);
    pushColor(packRgbc(r, g, b, code));
}

void Gte::pushColor(u32 packedRgbc)
{
    m_dataRegs[kDataRgb0] = m_dataRegs[kDataRgb1];
    m_dataRegs[kDataRgb1] = m_dataRegs[kDataRgb2];
    m_dataRegs[kDataRgb2] = packedRgbc;

    m_fifo.color[0] = m_dataRegs[kDataRgb0];
    m_fifo.color[1] = m_dataRegs[kDataRgb1];
    m_fifo.color[2] = m_dataRegs[kDataRgb2];
}

void Gte::updateLeadingZeroCount()
{
    m_dataRegs[kDataLzcr] = countLeadingMatchingBits(m_dataRegs[kDataLzcs]);
}

} // namespace runtime
} // namespace psxrecomp
