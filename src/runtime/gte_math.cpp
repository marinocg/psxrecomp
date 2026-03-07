#include "psxrecomp/runtime/gte.h"

#include "gte_internal.h"

namespace psxrecomp
{
namespace runtime
{

using namespace gte_detail;

void Gte::stallCpuCycles(u32 cycles)
{
    if (cycles == 0)
    {
        return;
    }
    if (m_cpuStallCallback)
    {
        m_cpuStallCallback(cycles);
    }
}

void Gte::stallForDataRead(u8 index)
{
    u32 stallCycles = m_busyCyclesRemaining;
    if ((index == kDataIrgb || index == kDataOrgb) && m_irgbReadBusyCyclesRemaining > stallCycles)
    {
        stallCycles = m_irgbReadBusyCyclesRemaining;
    }
    stallCpuCycles(stallCycles);
}

void Gte::stallForControlRead()
{
    stallCpuCycles(m_busyCyclesRemaining);
}

void Gte::stallForCommand()
{
    stallCpuCycles(m_busyCyclesRemaining);
}

void Gte::updateFlagSummaryBit()
{
    m_ctrlRegs[kCtrlFlag] &= ~(1u << 31);
    if ((m_ctrlRegs[kCtrlFlag] & kFlagErrorMask) != 0u)
    {
        m_ctrlRegs[kCtrlFlag] |= (1u << 31);
    }
}

void Gte::applyPendingIrgbWrites()
{
    if (m_pendingIrgbWriteValid && m_pendingIrgbIr3CyclesRemaining == 0)
    {
        m_dataRegs[kDataIr3] =
            static_cast<u32>(static_cast<s32>(((m_pendingIrgbPacked >> 10) & 0x1Fu) << 7));
    }

    if (m_pendingIrgbWriteValid && m_pendingIrgbIr12CyclesRemaining == 0)
    {
        m_dataRegs[kDataIr1] =
            static_cast<u32>(static_cast<s32>(((m_pendingIrgbPacked >> 0) & 0x1Fu) << 7));
        m_dataRegs[kDataIr2] =
            static_cast<u32>(static_cast<s32>(((m_pendingIrgbPacked >> 5) & 0x1Fu) << 7));
        m_dataRegs[kDataIr3] =
            static_cast<u32>(static_cast<s32>(((m_pendingIrgbPacked >> 10) & 0x1Fu) << 7));
        m_pendingIrgbPacked = 0;
        m_pendingIrgbWriteValid = false;
    }
}

u32 Gte::readDataRegister(u8 index) const
{
    switch (index)
    {
    case kDataSxyp:
        return m_dataRegs[kDataSxy2];
    case kDataIrgb:
    case kDataOrgb:
        return packIrgb(signExtend16(m_dataRegs[kDataIr1]), signExtend16(m_dataRegs[kDataIr2]),
                        signExtend16(m_dataRegs[kDataIr3]));
    case kDataLzcr:
        return m_dataRegs[kDataLzcr];
    default:
        return m_dataRegs[index];
    }
}

void Gte::writeDataRegister(u8 index, u32 value)
{
    switch (index)
    {
    case kDataVz0:
    case kDataVz1:
    case kDataVz2:
    case kDataIr1:
    case kDataIr2:
    case kDataIr3:
        m_dataRegs[index] = static_cast<u32>(signExtend16(value));
        if (index >= kDataIr1 && index <= kDataIr3)
        {
            m_pendingIrgbPacked = 0;
            m_pendingIrgbIr12CyclesRemaining = 0;
            m_pendingIrgbIr3CyclesRemaining = 0;
            m_irgbReadBusyCyclesRemaining = 0;
            m_pendingIrgbWriteValid = false;
        }
        break;
    case kDataIr0:
        m_dataRegs[index] = zeroExtend16(value);
        break;
    case kDataOtz:
    case kDataSz0:
    case kDataSz1:
    case kDataSz2:
    case kDataSz3:
        m_dataRegs[index] = zeroExtend16(value);
        break;
    case kDataSxy0:
    case kDataSxy1:
    case kDataSxy2:
        m_dataRegs[index] = value;
        m_fifo.screenXy[index - kDataSxy0] = m_dataRegs[index];
        m_dataRegs[kDataSxyp] = m_dataRegs[kDataSxy2];
        break;
    case kDataSxyp:
        pushScreenXy(lowHalfSigned(value), highHalfSigned(value));
        break;
    case kDataRgb0:
    case kDataRgb1:
    case kDataRgb2:
        m_dataRegs[index] = value;
        m_fifo.color[index - kDataRgb0] = value;
        break;
    case kDataIrgb:
    {
        const u32 packed = value & 0x7FFFu;
        m_dataRegs[kDataIrgb] = packed;
        m_dataRegs[kDataOrgb] = packed;
        m_pendingIrgbPacked = packed;
        m_pendingIrgbIr3CyclesRemaining = 2;
        m_pendingIrgbIr12CyclesRemaining = 3;
        m_irgbReadBusyCyclesRemaining = 3;
        m_pendingIrgbWriteValid = true;
        break;
    }
    case kDataLzcs:
        m_dataRegs[kDataLzcs] = value;
        updateLeadingZeroCount();
        break;
    case kDataLzcr:
        break;
    default:
        m_dataRegs[index] = value;
        break;
    }

    if (index >= kDataSz0 && index <= kDataSz3)
    {
        m_fifo.screenZ[index - kDataSz0] = static_cast<u16>(m_dataRegs[index] & 0xFFFFu);
    }
}

u32 Gte::readControlRegister(u8 index) const
{
    return m_ctrlRegs[index];
}

void Gte::writeControlRegister(u8 index, u32 value)
{
    switch (index)
    {
    case 4:
    case 12:
    case 20:
    case kCtrlDqa:
    case kCtrlZsf3:
    case kCtrlZsf4:
        m_ctrlRegs[index] = static_cast<u32>(signExtend16(value));
        break;
    case kCtrlH:
        m_ctrlRegs[index] = zeroExtend16(value);
        break;
    case kCtrlFlag:
        m_ctrlRegs[index] = value & 0x7FFFF000u;
        updateFlagSummaryBit();
        break;
    default:
        m_ctrlRegs[index] = value;
        break;
    }
}

Gte::Matrix3x3 Gte::readMatrix(u8 selector) const
{
    Matrix3x3 matrix{};
    const auto assignPacked = [&](u8 regBase)
    {
        matrix.rows[0][0] = lowHalfSigned(m_ctrlRegs[regBase + 0]);
        matrix.rows[0][1] = highHalfSigned(m_ctrlRegs[regBase + 0]);
        matrix.rows[0][2] = lowHalfSigned(m_ctrlRegs[regBase + 1]);
        matrix.rows[1][0] = highHalfSigned(m_ctrlRegs[regBase + 1]);
        matrix.rows[1][1] = lowHalfSigned(m_ctrlRegs[regBase + 2]);
        matrix.rows[1][2] = highHalfSigned(m_ctrlRegs[regBase + 2]);
        matrix.rows[2][0] = lowHalfSigned(m_ctrlRegs[regBase + 3]);
        matrix.rows[2][1] = highHalfSigned(m_ctrlRegs[regBase + 3]);
        matrix.rows[2][2] = signExtend16(m_ctrlRegs[regBase + 4]);
    };

    switch (selector & 0x3u)
    {
    case 0:
        assignPacked(0);
        break;
    case 1:
        assignPacked(8);
        break;
    case 2:
        assignPacked(16);
        break;
    case 3:
        matrix.rows[0][0] = -0x60;
        matrix.rows[0][1] = 0x60;
        matrix.rows[0][2] = signExtend16(m_dataRegs[kDataIr0]);
        matrix.rows[1][0] = lowHalfSigned(m_ctrlRegs[1]);
        matrix.rows[1][1] = lowHalfSigned(m_ctrlRegs[1]);
        matrix.rows[1][2] = lowHalfSigned(m_ctrlRegs[1]);
        matrix.rows[2][0] = lowHalfSigned(m_ctrlRegs[2]);
        matrix.rows[2][1] = lowHalfSigned(m_ctrlRegs[2]);
        matrix.rows[2][2] = lowHalfSigned(m_ctrlRegs[2]);
        break;
    }

    return matrix;
}

Gte::Vec3 Gte::readVector(u8 selector) const
{
    switch (selector & 0x3u)
    {
    case 0:
        return {lowHalfSigned(m_dataRegs[kDataVxy0]), highHalfSigned(m_dataRegs[kDataVxy0]),
                signExtend16(m_dataRegs[kDataVz0])};
    case 1:
        return {lowHalfSigned(m_dataRegs[kDataVxy1]), highHalfSigned(m_dataRegs[kDataVxy1]),
                signExtend16(m_dataRegs[kDataVz1])};
    case 2:
        return {lowHalfSigned(m_dataRegs[kDataVxy2]), highHalfSigned(m_dataRegs[kDataVxy2]),
                signExtend16(m_dataRegs[kDataVz2])};
    default:
        return {signExtend16(m_dataRegs[kDataIr1]), signExtend16(m_dataRegs[kDataIr2]),
                signExtend16(m_dataRegs[kDataIr3])};
    }
}

Gte::Vec3 Gte::readIrVector() const
{
    return {signExtend16(m_dataRegs[kDataIr1]), signExtend16(m_dataRegs[kDataIr2]),
            signExtend16(m_dataRegs[kDataIr3])};
}

Gte::Vec3 Gte::readTranslationVector(u8 selector) const
{
    switch (selector & 0x3u)
    {
    case 0:
        return {static_cast<s32>(m_ctrlRegs[kCtrlTrx]), static_cast<s32>(m_ctrlRegs[kCtrlTry]),
                static_cast<s32>(m_ctrlRegs[kCtrlTrz])};
    case 1:
        return {static_cast<s32>(m_ctrlRegs[kCtrlRbk]), static_cast<s32>(m_ctrlRegs[kCtrlGbk]),
                static_cast<s32>(m_ctrlRegs[kCtrlBbk])};
    case 2:
        return {static_cast<s32>(m_ctrlRegs[kCtrlRfc]), static_cast<s32>(m_ctrlRegs[kCtrlGfc]),
                static_cast<s32>(m_ctrlRegs[kCtrlBfc])};
    default:
        return {};
    }
}

Gte::ColorCode Gte::readRgbc() const
{
    const u32 packed = m_dataRegs[kDataRgbc];
    return {static_cast<u8>(packed & 0xFFu), static_cast<u8>((packed >> 8) & 0xFFu),
            static_cast<u8>((packed >> 16) & 0xFFu), static_cast<u8>((packed >> 24) & 0xFFu)};
}

Gte::ColorCode Gte::readColorFifoEntry(u8 fifoIndex) const
{
    const u8 index = static_cast<u8>(std::min<u8>(fifoIndex, 2));
    const u32 packed = m_dataRegs[kDataRgb0 + index];
    return {static_cast<u8>(packed & 0xFFu), static_cast<u8>((packed >> 8) & 0xFFu),
            static_cast<u8>((packed >> 16) & 0xFFu), static_cast<u8>((packed >> 24) & 0xFFu)};
}

Gte::ColorVec Gte::readMacVector() const
{
    return {signExtend32(m_dataRegs[kDataMac1]), signExtend32(m_dataRegs[kDataMac2]),
            signExtend32(m_dataRegs[kDataMac3])};
}

void Gte::clearCommandFlags()
{
    m_ctrlRegs[kCtrlFlag] = 0;
}
} // namespace runtime
} // namespace psxrecomp
