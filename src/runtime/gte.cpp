#include "psxrecomp/runtime/gte.h"

#include "gte_internal.h"

namespace psxrecomp
{
namespace runtime
{

using namespace gte_detail;

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

} // namespace runtime
} // namespace psxrecomp
