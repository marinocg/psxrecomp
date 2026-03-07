#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class Gte
{
  public:
    static constexpr size_t RegisterCount = 32;
    using CpuStallCallback = std::function<void(u32)>;

    void reset();
    void setCpuStallCallback(CpuStallCallback callback);

    u32 mfc2(u8 rd);
    void mtc2(u8 rd, u32 value);

    u32 cfc2(u8 rd);
    void ctc2(u8 rd, u32 value);

    void exec(u32 encoding);
    void tickCpuCycles(u32 cycles);
    u32 busyCyclesRemaining() const;

    std::vector<u8> serializeState() const;
    bool deserializeState(const std::vector<u8>& state);

  private:
    struct Vec3
    {
        s32 x = 0;
        s32 y = 0;
        s32 z = 0;
    };

    struct Matrix3x3
    {
        std::array<std::array<s32, 3>, 3> rows{};
    };

    struct ColorCode
    {
        u8 r = 0;
        u8 g = 0;
        u8 b = 0;
        u8 code = 0;
    };

    using ColorVec = std::array<s64, 3>;

    struct FifoState
    {
        // These mirror the architected screen/depth/color FIFOs backed by
        // data registers 12..22.
        std::array<u32, 3> screenXy{};
        std::array<u16, 4> screenZ{};
        std::array<u32, 3> color{};
    };

    static constexpr u8 normalizeRegisterIndex(u8 rd)
    {
        return rd & 0x1Fu;
    }

    static u32 estimateBusyCycles(u32 encoding);

    void stallCpuCycles(u32 cycles);
    void stallForDataRead(u8 index);
    void stallForControlRead();
    void stallForCommand();
    void updateFlagSummaryBit();
    void applyPendingIrgbWrites();

    u32 readDataRegister(u8 index) const;
    void writeDataRegister(u8 index, u32 value);

    u32 readControlRegister(u8 index) const;
    void writeControlRegister(u8 index, u32 value);

    Matrix3x3 readMatrix(u8 selector) const;
    Vec3 readVector(u8 selector) const;
    Vec3 readIrVector() const;
    Vec3 readTranslationVector(u8 selector) const;
    ColorCode readRgbc() const;
    ColorCode readColorFifoEntry(u8 fifoIndex) const;
    ColorVec readMacVector() const;

    void clearCommandFlags();
    void finalizeCommandFlags();
    void setFlag(u32 bit);

    void setMac0(s64 value);
    void setMac(u8 component, s64 value);
    void setMacVector(const ColorVec& values);
    s16 setIr(u8 component, s64 value, bool lm);
    void setIrVector(const ColorVec& values, bool lm);
    u16 setIr0(s64 value);

    ColorVec multiplyMatrix(const Matrix3x3& matrix, const Vec3& vector, const Vec3* translation,
                            bool sf, bool farColorBug = false) const;
    ColorVec multiplyRgbByIr(const ColorCode& color) const;
    ColorVec interpolateFarColor(const ColorVec& mac, bool sf);
    ColorVec applyShiftToColorVec(const ColorVec& mac, bool sf) const;
    void finalizeColorCommand(const ColorVec& values, bool lm, u8 code);

    void pushScreenXy(s16 sx, s16 sy);
    void pushScreenZ(u16 sz);
    void pushColorFromMac(u8 code);
    void pushColor(u32 packedRgbc);
    void updateLeadingZeroCount();

    void execRtps(u32 encoding);
    void execRtpt(u32 encoding);
    void execNclip();
    void execAvsz3();
    void execAvsz4();
    void execMvmva(u32 encoding);
    void execDpcs(u32 encoding);
    void execIntpl(u32 encoding);
    void execNcds(u32 encoding);
    void execCdp(u32 encoding);
    void execNcdt(u32 encoding);
    void execNccs(u32 encoding);
    void execCc(u32 encoding);
    void execNcs(u32 encoding);
    void execNct(u32 encoding);
    void execDcpl(u32 encoding);
    void execDpct(u32 encoding);
    void execGpf(u32 encoding);
    void execGpl(u32 encoding);
    void execNcct(u32 encoding);

    std::array<u32, RegisterCount> m_dataRegs{};
    std::array<u32, RegisterCount> m_ctrlRegs{};
    FifoState m_fifo{};
    u32 m_busyCyclesRemaining = 0;
    u32 m_irgbReadBusyCyclesRemaining = 0;
    u32 m_pendingIrgbPacked = 0;
    u32 m_pendingIrgbIr12CyclesRemaining = 0;
    u32 m_pendingIrgbIr3CyclesRemaining = 0;
    bool m_pendingIrgbWriteValid = false;
    CpuStallCallback m_cpuStallCallback;
};

} // namespace runtime
} // namespace psxrecomp
