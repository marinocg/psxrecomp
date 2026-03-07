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

namespace
{
constexpr u8 kDataVxy0 = 0;
constexpr u8 kDataVz0 = 1;
constexpr u8 kDataVxy1 = 2;
constexpr u8 kDataVz1 = 3;
constexpr u8 kDataVxy2 = 4;
constexpr u8 kDataVz2 = 5;
constexpr u8 kDataRgbc = 6;
constexpr u8 kDataOtz = 7;
constexpr u8 kDataIr0 = 8;
constexpr u8 kDataIr1 = 9;
constexpr u8 kDataIr2 = 10;
constexpr u8 kDataIr3 = 11;
constexpr u8 kDataSxy0 = 12;
constexpr u8 kDataSxy1 = 13;
constexpr u8 kDataSxy2 = 14;
constexpr u8 kDataSxyp = 15;
constexpr u8 kDataSz0 = 16;
constexpr u8 kDataSz1 = 17;
constexpr u8 kDataSz2 = 18;
constexpr u8 kDataSz3 = 19;
constexpr u8 kDataRgb0 = 20;
constexpr u8 kDataRgb1 = 21;
constexpr u8 kDataRgb2 = 22;
constexpr u8 kDataMac0 = 24;
constexpr u8 kDataMac1 = 25;
constexpr u8 kDataMac2 = 26;
constexpr u8 kDataMac3 = 27;
constexpr u8 kDataIrgb = 28;
constexpr u8 kDataOrgb = 29;
constexpr u8 kDataLzcs = 30;
constexpr u8 kDataLzcr = 31;

constexpr u8 kCtrlTrx = 5;
constexpr u8 kCtrlTry = 6;
constexpr u8 kCtrlTrz = 7;
constexpr u8 kCtrlRbk = 13;
constexpr u8 kCtrlGbk = 14;
constexpr u8 kCtrlBbk = 15;
constexpr u8 kCtrlRfc = 21;
constexpr u8 kCtrlGfc = 22;
constexpr u8 kCtrlBfc = 23;
constexpr u8 kCtrlOfx = 24;
constexpr u8 kCtrlOfy = 25;
constexpr u8 kCtrlH = 26;
constexpr u8 kCtrlDqa = 27;
constexpr u8 kCtrlDqb = 28;
constexpr u8 kCtrlZsf3 = 29;
constexpr u8 kCtrlZsf4 = 30;
constexpr u8 kCtrlFlag = 31;

constexpr u32 kFlagMac1Positive = 30;
constexpr u32 kFlagMac2Positive = 29;
constexpr u32 kFlagMac3Positive = 28;
constexpr u32 kFlagMac1Negative = 27;
constexpr u32 kFlagMac2Negative = 26;
constexpr u32 kFlagMac3Negative = 25;
constexpr u32 kFlagIr1Saturated = 24;
constexpr u32 kFlagIr2Saturated = 23;
constexpr u32 kFlagIr3Saturated = 22;
constexpr u32 kFlagColorRSaturated = 21;
constexpr u32 kFlagColorGSaturated = 20;
constexpr u32 kFlagColorBSaturated = 19;
constexpr u32 kFlagDepthSaturated = 18;
constexpr u32 kFlagDivideOverflow = 17;
constexpr u32 kFlagMac0Positive = 16;
constexpr u32 kFlagMac0Negative = 15;
constexpr u32 kFlagSx2Saturated = 14;
constexpr u32 kFlagSy2Saturated = 13;
constexpr u32 kFlagIr0Saturated = 12;
constexpr u32 kFlagErrorMask = 0x7F87E000u;

bool traceGteEnabled()
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

void traceGte(const char* fmt, ...)
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

constexpr s32 signExtend16(u32 value)
{
    return static_cast<s32>(static_cast<int16_t>(value & 0xFFFFu));
}

constexpr s64 signExtend32(u32 value)
{
    return static_cast<s64>(static_cast<s32>(value));
}

constexpr u32 zeroExtend16(u32 value)
{
    return value & 0xFFFFu;
}

constexpr s16 lowHalfSigned(u32 value)
{
    return static_cast<s16>(value & 0xFFFFu);
}

constexpr s16 highHalfSigned(u32 value)
{
    return static_cast<s16>((value >> 16) & 0xFFFFu);
}

u32 packHalfWords(s16 lo, s16 hi)
{
    return static_cast<u32>(static_cast<u16>(lo)) | (static_cast<u32>(static_cast<u16>(hi)) << 16);
}

u32 packRgbc(u8 r, u8 g, u8 b, u8 code)
{
    return static_cast<u32>(r) | (static_cast<u32>(g) << 8) | (static_cast<u32>(b) << 16) |
           (static_cast<u32>(code) << 24);
}

u32 packIrgb(s32 ir1, s32 ir2, s32 ir3)
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

unsigned countLeadingMatchingBits(u32 value)
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

s64 arithmeticShiftRight(s64 value, unsigned shift)
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

void appendU32(std::vector<u8>& out, u32 value)
{
    out.push_back(static_cast<u8>(value & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
}

bool consumeU32(const std::vector<u8>& data, size_t& cursor, u32& out)
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

} // namespace

void Gte::reset()
{
    m_dataRegs.fill(0);
    m_ctrlRegs.fill(0);
    m_fifo = {};
    m_busyCyclesRemaining = 0;
    m_irgbReadBusyCyclesRemaining = 0;
    m_pendingIrgbPacked = 0;
    m_pendingIrgbIr12CyclesRemaining = 0;
    m_pendingIrgbIr3CyclesRemaining = 0;
    m_pendingIrgbWriteValid = false;
}

void Gte::setCpuStallCallback(CpuStallCallback callback)
{
    m_cpuStallCallback = std::move(callback);
}

u32 Gte::mfc2(u8 rd)
{
    const u8 index = normalizeRegisterIndex(rd);
    stallForDataRead(index);
    applyPendingIrgbWrites();
    return readDataRegister(index);
}

void Gte::mtc2(u8 rd, u32 value)
{
    const u8 index = normalizeRegisterIndex(rd);
    writeDataRegister(index, value);
    traceGte("mtc2 rd=%u value=0x%08X", static_cast<unsigned>(index), value);
}

u32 Gte::cfc2(u8 rd)
{
    stallForControlRead();
    return readControlRegister(normalizeRegisterIndex(rd));
}

void Gte::ctc2(u8 rd, u32 value)
{
    const u8 index = normalizeRegisterIndex(rd);
    writeControlRegister(index, value);
    traceGte("ctc2 rd=%u value=0x%08X", static_cast<unsigned>(index), value);
}

void Gte::exec(u32 encoding)
{
    const u8 command = static_cast<u8>(encoding & 0x3Fu);
    stallForCommand();
    applyPendingIrgbWrites();
    m_busyCyclesRemaining = estimateBusyCycles(encoding);
    clearCommandFlags();

    switch (command)
    {
    case 0x01:
        execRtps(encoding);
        break;
    case 0x06:
        execNclip();
        break;
    case 0x10:
        execDpcs(encoding);
        break;
    case 0x11:
        execIntpl(encoding);
        break;
    case 0x12:
        execMvmva(encoding);
        break;
    case 0x13:
        execNcds(encoding);
        break;
    case 0x14:
        execCdp(encoding);
        break;
    case 0x16:
        execNcdt(encoding);
        break;
    case 0x1B:
        execNccs(encoding);
        break;
    case 0x1C:
        execCc(encoding);
        break;
    case 0x1E:
        execNcs(encoding);
        break;
    case 0x20:
        execNct(encoding);
        break;
    case 0x29:
        execDcpl(encoding);
        break;
    case 0x2A:
        execDpct(encoding);
        break;
    case 0x2D:
        execAvsz3();
        break;
    case 0x2E:
        execAvsz4();
        break;
    case 0x30:
        execRtpt(encoding);
        break;
    case 0x3D:
        execGpf(encoding);
        break;
    case 0x3E:
        execGpl(encoding);
        break;
    case 0x3F:
        execNcct(encoding);
        break;
    default:
        traceGte("exec raw=0x%08X opcode=0x%02X unsupported", encoding, command);
        finalizeCommandFlags();
        return;
    }

    finalizeCommandFlags();
    traceGte("exec raw=0x%08X opcode=0x%02X flag=0x%08X", encoding, command, m_ctrlRegs[kCtrlFlag]);
}

void Gte::tickCpuCycles(u32 cycles)
{
    if (cycles >= m_busyCyclesRemaining)
    {
        m_busyCyclesRemaining = 0;
    }
    else
    {
        m_busyCyclesRemaining -= cycles;
    }

    if (cycles >= m_irgbReadBusyCyclesRemaining)
    {
        m_irgbReadBusyCyclesRemaining = 0;
    }
    else
    {
        m_irgbReadBusyCyclesRemaining -= cycles;
    }

    if (cycles >= m_pendingIrgbIr3CyclesRemaining)
    {
        m_pendingIrgbIr3CyclesRemaining = 0;
    }
    else
    {
        m_pendingIrgbIr3CyclesRemaining -= cycles;
    }

    if (cycles >= m_pendingIrgbIr12CyclesRemaining)
    {
        m_pendingIrgbIr12CyclesRemaining = 0;
    }
    else
    {
        m_pendingIrgbIr12CyclesRemaining -= cycles;
    }

    applyPendingIrgbWrites();
}

u32 Gte::busyCyclesRemaining() const
{
    return m_busyCyclesRemaining;
}

std::vector<u8> Gte::serializeState() const
{
    std::vector<u8> state;
    state.reserve((RegisterCount * 2 + 3 + 4 + 3 + 6) * sizeof(u32));
    for (u32 value : m_dataRegs)
    {
        appendU32(state, value);
    }
    for (u32 value : m_ctrlRegs)
    {
        appendU32(state, value);
    }
    for (u32 value : m_fifo.screenXy)
    {
        appendU32(state, value);
    }
    for (u16 value : m_fifo.screenZ)
    {
        appendU32(state, value);
    }
    for (u32 value : m_fifo.color)
    {
        appendU32(state, value);
    }
    appendU32(state, m_busyCyclesRemaining);
    appendU32(state, m_irgbReadBusyCyclesRemaining);
    appendU32(state, m_pendingIrgbPacked);
    appendU32(state, m_pendingIrgbIr12CyclesRemaining);
    appendU32(state, m_pendingIrgbIr3CyclesRemaining);
    appendU32(state, m_pendingIrgbWriteValid ? 1u : 0u);
    return state;
}

bool Gte::deserializeState(const std::vector<u8>& state)
{
    size_t cursor = 0;

    for (u32& value : m_dataRegs)
    {
        if (!consumeU32(state, cursor, value))
        {
            return false;
        }
    }
    for (u32& value : m_ctrlRegs)
    {
        if (!consumeU32(state, cursor, value))
        {
            return false;
        }
    }
    for (u32& value : m_fifo.screenXy)
    {
        if (!consumeU32(state, cursor, value))
        {
            return false;
        }
    }
    for (u16& value : m_fifo.screenZ)
    {
        u32 serialized = 0;
        if (!consumeU32(state, cursor, serialized))
        {
            return false;
        }
        value = static_cast<u16>(serialized & 0xFFFFu);
    }
    for (u32& value : m_fifo.color)
    {
        if (!consumeU32(state, cursor, value))
        {
            return false;
        }
    }

    u32 pendingIrgbWriteValid = 0;
    if (!consumeU32(state, cursor, m_busyCyclesRemaining) ||
        !consumeU32(state, cursor, m_irgbReadBusyCyclesRemaining) ||
        !consumeU32(state, cursor, m_pendingIrgbPacked) ||
        !consumeU32(state, cursor, m_pendingIrgbIr12CyclesRemaining) ||
        !consumeU32(state, cursor, m_pendingIrgbIr3CyclesRemaining) ||
        !consumeU32(state, cursor, pendingIrgbWriteValid))
    {
        return false;
    }
    m_pendingIrgbWriteValid = pendingIrgbWriteValid != 0u;

    if (cursor != state.size())
    {
        return false;
    }

    updateFlagSummaryBit();
    return true;
}

u32 Gte::estimateBusyCycles(u32 encoding)
{
    switch (encoding & 0x3Fu)
    {
    case 0x01:
        return 15;
    case 0x06:
        return 8;
    case 0x10:
        return 8;
    case 0x11:
        return 8;
    case 0x12:
        return 8;
    case 0x13:
        return 19;
    case 0x14:
        return 13;
    case 0x16:
        return 44;
    case 0x1B:
        return 17;
    case 0x1C:
        return 11;
    case 0x1E:
        return 14;
    case 0x20:
        return 30;
    case 0x29:
        return 8;
    case 0x2A:
        return 17;
    case 0x2D:
        return 5;
    case 0x2E:
        return 6;
    case 0x30:
        return 23;
    case 0x3D:
        return 5;
    case 0x3E:
        return 5;
    case 0x3F:
        return 19;
    default:
        return 8;
    }
}

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
