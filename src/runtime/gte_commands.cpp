#include "psxrecomp/runtime/gte.h"

#include "gte_internal.h"

namespace psxrecomp
{
namespace runtime
{

using namespace gte_detail;

void Gte::execRtps(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const Gte::Vec3 vector = readVector(0);
    const Gte::Matrix3x3 matrix = readMatrix(0);
    const Gte::Vec3 translation = readTranslationVector(0);

    const s64 shift = sf ? 12 : 0;
    const s64 baseX = static_cast<s64>(translation.x) * 4096 +
                      static_cast<s64>(matrix.rows[0][0]) * vector.x +
                      static_cast<s64>(matrix.rows[0][1]) * vector.y +
                      static_cast<s64>(matrix.rows[0][2]) * vector.z;
    const s64 baseY = static_cast<s64>(translation.y) * 4096 +
                      static_cast<s64>(matrix.rows[1][0]) * vector.x +
                      static_cast<s64>(matrix.rows[1][1]) * vector.y +
                      static_cast<s64>(matrix.rows[1][2]) * vector.z;
    const s64 baseZ = static_cast<s64>(translation.z) * 4096 +
                      static_cast<s64>(matrix.rows[2][0]) * vector.x +
                      static_cast<s64>(matrix.rows[2][1]) * vector.y +
                      static_cast<s64>(matrix.rows[2][2]) * vector.z;

    const s64 mac1 = arithmeticShiftRight(baseX, static_cast<unsigned>(shift));
    const s64 mac2 = arithmeticShiftRight(baseY, static_cast<unsigned>(shift));
    const s64 mac3 = arithmeticShiftRight(baseZ, static_cast<unsigned>(shift));
    setMac(1, mac1);
    setMac(2, mac2);
    setMac(3, mac3);

    const s16 ir1 = setIr(1, mac1, false);
    const s16 ir2 = setIr(2, mac2, false);
    setIr(3, mac3, false);

    s64 sz3Value = arithmeticShiftRight(baseZ, 12);
    if (sz3Value < 0)
    {
        sz3Value = 0;
        setFlag(kFlagDepthSaturated);
    }
    else if (sz3Value > 0xFFFF)
    {
        sz3Value = 0xFFFF;
        setFlag(kFlagDepthSaturated);
    }
    const u16 sz3 = static_cast<u16>(sz3Value);
    pushScreenZ(sz3);

    u32 perspective = 0x1FFFFu;
    const u32 h = static_cast<u32>(m_ctrlRegs[kCtrlH] & 0xFFFFu);
    if (sz3 == 0)
    {
        setFlag(kFlagDivideOverflow);
    }
    else
    {
        const u64 quotient = ((static_cast<u64>(h) << 17) / sz3 + 1u) >> 1;
        if (quotient > 0x1FFFFu)
        {
            setFlag(kFlagDivideOverflow);
        }
        perspective = static_cast<u32>(std::min<u64>(quotient, 0x1FFFFu));
    }

    const s64 sxValue =
        arithmeticShiftRight(static_cast<s64>(static_cast<s32>(m_ctrlRegs[kCtrlOfx])) +
                                 static_cast<s64>(perspective) * ir1,
                             16);
    const s64 syValue =
        arithmeticShiftRight(static_cast<s64>(static_cast<s32>(m_ctrlRegs[kCtrlOfy])) +
                                 static_cast<s64>(perspective) * ir2,
                             16);
    s16 sx = 0;
    if (sxValue > 0x3FF)
    {
        sx = 0x03FF;
        setFlag(kFlagSx2Saturated);
    }
    else if (sxValue < -0x400)
    {
        sx = static_cast<s16>(-0x400);
        setFlag(kFlagSx2Saturated);
    }
    else
    {
        sx = static_cast<s16>(sxValue);
    }

    s16 sy = 0;
    if (syValue > 0x3FF)
    {
        sy = 0x03FF;
        setFlag(kFlagSy2Saturated);
    }
    else if (syValue < -0x400)
    {
        sy = static_cast<s16>(-0x400);
        setFlag(kFlagSy2Saturated);
    }
    else
    {
        sy = static_cast<s16>(syValue);
    }
    pushScreenXy(sx, sy);

    const s64 depthCue = static_cast<s64>(signExtend16(m_ctrlRegs[kCtrlDqa])) * perspective +
                         static_cast<s64>(static_cast<s32>(m_ctrlRegs[kCtrlDqb]));
    setMac0(depthCue);
    setIr0(arithmeticShiftRight(depthCue, 12));
}

void Gte::execRtpt(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const Gte::Matrix3x3 matrix = readMatrix(0);
    const Gte::Vec3 translation = readTranslationVector(0);

    u32 lastPerspective = 0x1FFFFu;
    s16 lastIr1 = 0;
    s16 lastIr2 = 0;

    for (u8 vectorIndex = 0; vectorIndex < 3; ++vectorIndex)
    {
        const Gte::Vec3 vector = readVector(vectorIndex);
        const s64 shift = sf ? 12 : 0;
        const s64 baseX = static_cast<s64>(translation.x) * 4096 +
                          static_cast<s64>(matrix.rows[0][0]) * vector.x +
                          static_cast<s64>(matrix.rows[0][1]) * vector.y +
                          static_cast<s64>(matrix.rows[0][2]) * vector.z;
        const s64 baseY = static_cast<s64>(translation.y) * 4096 +
                          static_cast<s64>(matrix.rows[1][0]) * vector.x +
                          static_cast<s64>(matrix.rows[1][1]) * vector.y +
                          static_cast<s64>(matrix.rows[1][2]) * vector.z;
        const s64 baseZ = static_cast<s64>(translation.z) * 4096 +
                          static_cast<s64>(matrix.rows[2][0]) * vector.x +
                          static_cast<s64>(matrix.rows[2][1]) * vector.y +
                          static_cast<s64>(matrix.rows[2][2]) * vector.z;

        const s64 mac1 = arithmeticShiftRight(baseX, static_cast<unsigned>(shift));
        const s64 mac2 = arithmeticShiftRight(baseY, static_cast<unsigned>(shift));
        const s64 mac3 = arithmeticShiftRight(baseZ, static_cast<unsigned>(shift));
        setMac(1, mac1);
        setMac(2, mac2);
        setMac(3, mac3);

        const s16 ir1 = setIr(1, mac1, false);
        const s16 ir2 = setIr(2, mac2, false);
        setIr(3, mac3, false);

        s64 sz3Value = arithmeticShiftRight(baseZ, 12);
        if (sz3Value < 0)
        {
            sz3Value = 0;
            setFlag(kFlagDepthSaturated);
        }
        else if (sz3Value > 0xFFFF)
        {
            sz3Value = 0xFFFF;
            setFlag(kFlagDepthSaturated);
        }
        const u16 sz3 = static_cast<u16>(sz3Value);
        pushScreenZ(sz3);

        const u32 h = static_cast<u32>(m_ctrlRegs[kCtrlH] & 0xFFFFu);
        if (sz3 == 0)
        {
            setFlag(kFlagDivideOverflow);
            lastPerspective = 0x1FFFFu;
        }
        else
        {
            const u64 quotient = ((static_cast<u64>(h) << 17) / sz3 + 1u) >> 1;
            if (quotient > 0x1FFFFu)
            {
                setFlag(kFlagDivideOverflow);
            }
            lastPerspective = static_cast<u32>(std::min<u64>(quotient, 0x1FFFFu));
        }

        const s64 sxValue =
            arithmeticShiftRight(static_cast<s64>(static_cast<s32>(m_ctrlRegs[kCtrlOfx])) +
                                     static_cast<s64>(lastPerspective) * ir1,
                                 16);
        const s64 syValue =
            arithmeticShiftRight(static_cast<s64>(static_cast<s32>(m_ctrlRegs[kCtrlOfy])) +
                                     static_cast<s64>(lastPerspective) * ir2,
                                 16);
        s16 sx = 0;
        if (sxValue > 0x3FF)
        {
            sx = 0x03FF;
            setFlag(kFlagSx2Saturated);
        }
        else if (sxValue < -0x400)
        {
            sx = static_cast<s16>(-0x400);
            setFlag(kFlagSx2Saturated);
        }
        else
        {
            sx = static_cast<s16>(sxValue);
        }

        s16 sy = 0;
        if (syValue > 0x3FF)
        {
            sy = 0x03FF;
            setFlag(kFlagSy2Saturated);
        }
        else if (syValue < -0x400)
        {
            sy = static_cast<s16>(-0x400);
            setFlag(kFlagSy2Saturated);
        }
        else
        {
            sy = static_cast<s16>(syValue);
        }
        pushScreenXy(sx, sy);

        lastIr1 = ir1;
        lastIr2 = ir2;
    }

    const s64 depthCue = static_cast<s64>(signExtend16(m_ctrlRegs[kCtrlDqa])) * lastPerspective +
                         static_cast<s64>(static_cast<s32>(m_ctrlRegs[kCtrlDqb]));
    setMac0(depthCue);
    setIr0(arithmeticShiftRight(depthCue, 12));

    traceGte("rtpt sxy0=0x%08X sxy1=0x%08X sxy2=0x%08X sz1=%u sz2=%u sz3=%u ir=(%d,%d,%d)",
             m_dataRegs[kDataSxy0], m_dataRegs[kDataSxy1], m_dataRegs[kDataSxy2],
             static_cast<unsigned>(m_dataRegs[kDataSz1] & 0xFFFFu),
             static_cast<unsigned>(m_dataRegs[kDataSz2] & 0xFFFFu),
             static_cast<unsigned>(m_dataRegs[kDataSz3] & 0xFFFFu), lastIr1, lastIr2,
             signExtend16(m_dataRegs[kDataIr3]));
}

void Gte::execNclip()
{
    const s32 sx0 = lowHalfSigned(m_dataRegs[kDataSxy0]);
    const s32 sy0 = highHalfSigned(m_dataRegs[kDataSxy0]);
    const s32 sx1 = lowHalfSigned(m_dataRegs[kDataSxy1]);
    const s32 sy1 = highHalfSigned(m_dataRegs[kDataSxy1]);
    const s32 sx2 = lowHalfSigned(m_dataRegs[kDataSxy2]);
    const s32 sy2 = highHalfSigned(m_dataRegs[kDataSxy2]);

    const s64 value = static_cast<s64>(sx0) * sy1 + static_cast<s64>(sx1) * sy2 +
                      static_cast<s64>(sx2) * sy0 - static_cast<s64>(sx0) * sy2 -
                      static_cast<s64>(sx1) * sy0 - static_cast<s64>(sx2) * sy1;
    setMac0(value);
}

void Gte::execAvsz3()
{
    const s64 sum = static_cast<s64>(m_dataRegs[kDataSz1] & 0xFFFFu) +
                    static_cast<s64>(m_dataRegs[kDataSz2] & 0xFFFFu) +
                    static_cast<s64>(m_dataRegs[kDataSz3] & 0xFFFFu);
    const s64 value = static_cast<s64>(signExtend16(m_ctrlRegs[kCtrlZsf3])) * sum;
    setMac0(value);
    s64 otz = arithmeticShiftRight(value, 12);
    if (otz < 0)
    {
        otz = 0;
        setFlag(kFlagDepthSaturated);
    }
    else if (otz > 0xFFFF)
    {
        otz = 0xFFFF;
        setFlag(kFlagDepthSaturated);
    }
    m_dataRegs[kDataOtz] = static_cast<u32>(otz);
}

void Gte::execAvsz4()
{
    const s64 sum = static_cast<s64>(m_dataRegs[kDataSz0] & 0xFFFFu) +
                    static_cast<s64>(m_dataRegs[kDataSz1] & 0xFFFFu) +
                    static_cast<s64>(m_dataRegs[kDataSz2] & 0xFFFFu) +
                    static_cast<s64>(m_dataRegs[kDataSz3] & 0xFFFFu);
    const s64 value = static_cast<s64>(signExtend16(m_ctrlRegs[kCtrlZsf4])) * sum;
    setMac0(value);
    s64 otz = arithmeticShiftRight(value, 12);
    if (otz < 0)
    {
        otz = 0;
        setFlag(kFlagDepthSaturated);
    }
    else if (otz > 0xFFFF)
    {
        otz = 0xFFFF;
        setFlag(kFlagDepthSaturated);
    }
    m_dataRegs[kDataOtz] = static_cast<u32>(otz);
}

void Gte::execMvmva(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const u8 mx = static_cast<u8>((encoding >> 17) & 0x3u);
    const u8 vectorSelector = static_cast<u8>((encoding >> 15) & 0x3u);
    const u8 translationSelector = static_cast<u8>((encoding >> 13) & 0x3u);
    const bool lm = (encoding & (1u << 10)) != 0u;

    const Gte::Matrix3x3 matrix = readMatrix(mx);
    const Gte::Vec3 vector = readVector(vectorSelector);
    const Gte::Vec3 translation = readTranslationVector(translationSelector);
    const ColorVec values =
        multiplyMatrix(matrix, vector, translationSelector == 2 ? nullptr : &translation, sf,
                       translationSelector == 2);
    setMacVector(values);
    setIrVector(values, lm);
}

void Gte::execDpcs(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const ColorCode color = readRgbc();
    const ColorVec base = {static_cast<s64>(color.r) << 16, static_cast<s64>(color.g) << 16,
                           static_cast<s64>(color.b) << 16};
    const ColorVec finalValues = applyShiftToColorVec(interpolateFarColor(base, sf), sf);
    finalizeColorCommand(finalValues, lm, color.code);
}

void Gte::execIntpl(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const Vec3 ir = readIrVector();
    const ColorVec base = {static_cast<s64>(ir.x) << 12, static_cast<s64>(ir.y) << 12,
                           static_cast<s64>(ir.z) << 12};
    const ColorVec finalValues = applyShiftToColorVec(interpolateFarColor(base, sf), sf);
    finalizeColorCommand(finalValues, lm, readRgbc().code);
}

} // namespace runtime
} // namespace psxrecomp
