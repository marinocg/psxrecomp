#include "psxrecomp/runtime/gte.h"

#include "gte_internal.h"

namespace psxrecomp
{
namespace runtime
{

using namespace gte_detail;

void Gte::execNcds(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const ColorCode color = readRgbc();

    const ColorVec light = multiplyMatrix(readMatrix(1), readVector(0), nullptr, sf);
    setMacVector(light);
    setIrVector(light, lm);

    const Vec3 background = readTranslationVector(1);
    const ColorVec shaded = multiplyMatrix(readMatrix(2), readIrVector(), &background, sf);
    setMacVector(shaded);
    setIrVector(shaded, lm);

    const ColorVec finalValues =
        applyShiftToColorVec(interpolateFarColor(multiplyRgbByIr(color), sf), sf);
    finalizeColorCommand(finalValues, lm, color.code);
}

void Gte::execCdp(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const ColorCode color = readRgbc();
    const Vec3 background = readTranslationVector(1);

    const ColorVec shaded = multiplyMatrix(readMatrix(2), readIrVector(), &background, sf);
    setMacVector(shaded);
    setIrVector(shaded, lm);

    const ColorVec finalValues =
        applyShiftToColorVec(interpolateFarColor(multiplyRgbByIr(color), sf), sf);
    finalizeColorCommand(finalValues, lm, color.code);
}

void Gte::execNcdt(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const ColorCode color = readRgbc();
    const Vec3 background = readTranslationVector(1);

    for (u8 vectorIndex = 0; vectorIndex < 3; ++vectorIndex)
    {
        const ColorVec light = multiplyMatrix(readMatrix(1), readVector(vectorIndex), nullptr, sf);
        setMacVector(light);
        setIrVector(light, lm);

        const ColorVec shaded = multiplyMatrix(readMatrix(2), readIrVector(), &background, sf);
        setMacVector(shaded);
        setIrVector(shaded, lm);

        const ColorVec finalValues =
            applyShiftToColorVec(interpolateFarColor(multiplyRgbByIr(color), sf), sf);
        finalizeColorCommand(finalValues, lm, color.code);
    }
}

void Gte::execNccs(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const ColorCode color = readRgbc();
    const Vec3 background = readTranslationVector(1);

    const ColorVec light = multiplyMatrix(readMatrix(1), readVector(0), nullptr, sf);
    setMacVector(light);
    setIrVector(light, lm);

    const ColorVec shaded = multiplyMatrix(readMatrix(2), readIrVector(), &background, sf);
    setMacVector(shaded);
    setIrVector(shaded, lm);

    finalizeColorCommand(applyShiftToColorVec(multiplyRgbByIr(color), sf), lm, color.code);
}

void Gte::execCc(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const ColorCode color = readRgbc();
    const Vec3 background = readTranslationVector(1);

    const ColorVec shaded = multiplyMatrix(readMatrix(2), readIrVector(), &background, sf);
    setMacVector(shaded);
    setIrVector(shaded, lm);

    finalizeColorCommand(applyShiftToColorVec(multiplyRgbByIr(color), sf), lm, color.code);
}

void Gte::execNcs(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const Vec3 background = readTranslationVector(1);

    const ColorVec light = multiplyMatrix(readMatrix(1), readVector(0), nullptr, sf);
    setMacVector(light);
    setIrVector(light, lm);

    const ColorVec shaded = multiplyMatrix(readMatrix(2), readIrVector(), &background, sf);
    finalizeColorCommand(shaded, lm, readRgbc().code);
}

void Gte::execNct(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const Vec3 background = readTranslationVector(1);
    const u8 code = readRgbc().code;

    for (u8 vectorIndex = 0; vectorIndex < 3; ++vectorIndex)
    {
        const ColorVec light = multiplyMatrix(readMatrix(1), readVector(vectorIndex), nullptr, sf);
        setMacVector(light);
        setIrVector(light, lm);

        const ColorVec shaded = multiplyMatrix(readMatrix(2), readIrVector(), &background, sf);
        finalizeColorCommand(shaded, lm, code);
    }
}

void Gte::execDcpl(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const ColorCode color = readRgbc();
    const ColorVec finalValues =
        applyShiftToColorVec(interpolateFarColor(multiplyRgbByIr(color), sf), sf);
    finalizeColorCommand(finalValues, lm, color.code);
}

void Gte::execDpct(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const u8 code = readRgbc().code;

    for (u8 iteration = 0; iteration < 3; ++iteration)
    {
        const ColorCode color = readColorFifoEntry(0);
        const ColorVec base = {static_cast<s64>(color.r) << 16, static_cast<s64>(color.g) << 16,
                               static_cast<s64>(color.b) << 16};
        const ColorVec finalValues = applyShiftToColorVec(interpolateFarColor(base, sf), sf);
        finalizeColorCommand(finalValues, lm, code);
    }
}

void Gte::execGpf(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const Vec3 ir = readIrVector();
    const u16 ir0 = static_cast<u16>(m_dataRegs[kDataIr0] & 0xFFFFu);
    const unsigned shift = sf ? 12 : 0;
    const ColorVec values = {arithmeticShiftRight(static_cast<s64>(ir.x) * ir0, shift),
                             arithmeticShiftRight(static_cast<s64>(ir.y) * ir0, shift),
                             arithmeticShiftRight(static_cast<s64>(ir.z) * ir0, shift)};
    finalizeColorCommand(values, lm, readRgbc().code);
}

void Gte::execGpl(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const Vec3 ir = readIrVector();
    const u16 ir0 = static_cast<u16>(m_dataRegs[kDataIr0] & 0xFFFFu);
    const ColorVec base = readMacVector();
    const unsigned shift = sf ? 12 : 0;
    ColorVec values{};

    for (u8 component = 0; component < 3; ++component)
    {
        const s64 preserved = sf ? (base[component] << 12) : base[component];
        const s32 irComponent = component == 0 ? ir.x : (component == 1 ? ir.y : ir.z);
        values[component] =
            arithmeticShiftRight(static_cast<s64>(irComponent) * ir0 + preserved, shift);
    }

    finalizeColorCommand(values, lm, readRgbc().code);
}

void Gte::execNcct(u32 encoding)
{
    const bool sf = (encoding & (1u << 19)) != 0u;
    const bool lm = (encoding & (1u << 10)) != 0u;
    const ColorCode color = readRgbc();
    const Vec3 background = readTranslationVector(1);

    for (u8 vectorIndex = 0; vectorIndex < 3; ++vectorIndex)
    {
        const ColorVec light = multiplyMatrix(readMatrix(1), readVector(vectorIndex), nullptr, sf);
        setMacVector(light);
        setIrVector(light, lm);

        const ColorVec shaded = multiplyMatrix(readMatrix(2), readIrVector(), &background, sf);
        setMacVector(shaded);
        setIrVector(shaded, lm);

        finalizeColorCommand(applyShiftToColorVec(multiplyRgbByIr(color), sf), lm, color.code);
    }
}
} // namespace runtime
} // namespace psxrecomp
