#include "psxrecomp/runtime/psx_system.h"

#include <cstdlib>
#include <sstream>
#include <string>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr Address CrashGpuWaitEntryPc = 0x8003E4F0u;
constexpr Address CrashGpuWaitInitialReadPc = 0x8003E510u;
constexpr Address CrashGpuWaitBit19BranchPc = 0x8003E5B4u;
constexpr Address CrashGpuWaitCompareReadPc = 0x8003E5C8u;
constexpr Address CrashGpuWaitCompareBranchPc = 0x8003E5D4u;
constexpr Address CrashGpuWaitLoopReadPc = 0x8003E5E0u;
constexpr Address CrashGpuWaitLoopBranchPc = 0x8003E5F0u;
constexpr u32 CrashGpuWaitBit19Mask = 0x00080000u;
constexpr u32 CrashGpuWaitBit31Mask = 0x80000000u;

const char* gpuPortLabel(Address address)
{
    switch (address)
    {
    case Mmio::GPU_GP0:
        return "GP0";
    case Mmio::GPU_GP1:
        return "GP1";
    default:
        return "UNKNOWN";
    }
}

bool envFlagEnabled(const char* name)
{
    if (const char* env = std::getenv(name))
    {
        return env[0] == '1';
    }
    return false;
}
} // namespace

void PsxSystem::observeProgramCounter(Address pc)
{
    m_debugOverlay.setLastProgramCounter(pc);
    m_stallClassifier.recordPc(pc);
    traceGpuWaitProgramCounter(pc);
    traceDisplayTimingProgramCounter(pc);
}

bool PsxSystem::gpuWaitTraceEnabled()
{
    if (!m_gpuWaitTrace.configured)
    {
        m_gpuWaitTrace.enabled = envFlagEnabled("PSXRECOMP_TRACE_GPU_WAIT");
        m_gpuWaitTrace.configured = true;
    }
    return m_gpuWaitTrace.enabled;
}

void PsxSystem::recordGpuPortTrace(Address address, u32 value)
{
    if (!gpuWaitTraceEnabled())
    {
        return;
    }

    GpuPortTraceEntry entry;
    entry.pc = m_debugOverlay.lastProgramCounter();
    entry.address = address;
    entry.value = value;
    entry.sequence = ++m_gpuWaitTrace.sequence;

    m_gpuWaitTrace.recentWrites[m_gpuWaitTrace.recentWriteHead] = entry;
    m_gpuWaitTrace.recentWriteHead =
        (m_gpuWaitTrace.recentWriteHead + 1) % m_gpuWaitTrace.recentWrites.size();
    if (m_gpuWaitTrace.recentWriteCount < m_gpuWaitTrace.recentWrites.size())
    {
        ++m_gpuWaitTrace.recentWriteCount;
    }
}

std::string PsxSystem::formatRecentGpuPortWrites() const
{
    if (m_gpuWaitTrace.recentWriteCount == 0)
    {
        return "none";
    }

    std::ostringstream stream;
    for (size_t i = 0; i < m_gpuWaitTrace.recentWriteCount; ++i)
    {
        const size_t index = (m_gpuWaitTrace.recentWriteHead + m_gpuWaitTrace.recentWrites.size() -
                              m_gpuWaitTrace.recentWriteCount + i) %
                             m_gpuWaitTrace.recentWrites.size();
        const auto& entry = m_gpuWaitTrace.recentWrites[index];
        if (i != 0)
        {
            stream << "; ";
        }
        stream << '#' << entry.sequence << " pc=0x" << std::hex << entry.pc << " "
               << gpuPortLabel(entry.address) << "=0x" << entry.value;
    }
    return stream.str();
}

void PsxSystem::traceGpuWaitStatusRead(Address pc, u32 value)
{
    if (!gpuWaitTraceEnabled())
    {
        return;
    }

    const char* phase = nullptr;
    if (pc == CrashGpuWaitInitialReadPc)
    {
        m_gpuWaitTrace.hasInitialStatus = true;
        m_gpuWaitTrace.initialStatus = value;
        m_gpuWaitTrace.hasCompareStatus = false;
        m_gpuWaitTrace.hasLoopStatus = false;
        phase = "initial";
    }
    else if (pc == CrashGpuWaitCompareReadPc)
    {
        m_gpuWaitTrace.hasCompareStatus = true;
        m_gpuWaitTrace.compareStatus = value;
        phase = "compare";
    }
    else if (pc == CrashGpuWaitLoopReadPc)
    {
        m_gpuWaitTrace.hasLoopStatus = true;
        m_gpuWaitTrace.loopStatus = value;
        phase = "loop";
    }
    else
    {
        return;
    }

    std::ostringstream msg;
    msg << "event=status_read phase=" << phase << " pc=0x" << std::hex << pc << " raw=0x"
        << value << " bit19=" << std::dec << ((value & CrashGpuWaitBit19Mask) != 0)
        << " bit31=" << ((value & CrashGpuWaitBit31Mask) != 0)
        << " recent_writes=" << formatRecentGpuPortWrites();
    m_logger.log(LogLevel::Info, "gpu_wait", msg.str());
}

void PsxSystem::traceGpuWaitProgramCounter(Address pc)
{
    if (!gpuWaitTraceEnabled())
    {
        return;
    }

    std::ostringstream msg;
    switch (pc)
    {
    case CrashGpuWaitEntryPc:
        ++m_gpuWaitTrace.helperEntries;
        m_gpuWaitTrace.hasInitialStatus = false;
        m_gpuWaitTrace.hasCompareStatus = false;
        m_gpuWaitTrace.hasLoopStatus = false;
        msg << "event=helper_entry pc=0x" << std::hex << pc << " count=" << std::dec
            << m_gpuWaitTrace.helperEntries << " recent_writes=" << formatRecentGpuPortWrites();
        break;
    case CrashGpuWaitBit19BranchPc:
        if (!m_gpuWaitTrace.hasInitialStatus)
        {
            msg << "event=bit19_gate pc=0x" << std::hex << pc << " status=missing";
            break;
        }
        msg << "event=bit19_gate pc=0x" << std::hex << pc << " raw=0x"
            << m_gpuWaitTrace.initialStatus << " mask=0x" << CrashGpuWaitBit19Mask
            << " result=0x"
            << (m_gpuWaitTrace.initialStatus & CrashGpuWaitBit19Mask) << " branch="
            << (((m_gpuWaitTrace.initialStatus & CrashGpuWaitBit19Mask) == 0) ? "skip_wait"
                                                                         : "arm_wait");
        break;
    case CrashGpuWaitCompareBranchPc:
        if (!m_gpuWaitTrace.hasInitialStatus || !m_gpuWaitTrace.hasCompareStatus)
        {
            msg << "event=field_flip_check pc=0x" << std::hex << pc << " status=missing";
            break;
        }
        msg << "event=field_flip_check pc=0x" << std::hex << pc << " initial=0x"
            << m_gpuWaitTrace.initialStatus << " compare=0x" << m_gpuWaitTrace.compareStatus
            << " xor=0x"
            << (m_gpuWaitTrace.initialStatus ^ m_gpuWaitTrace.compareStatus) << " mask=0x"
            << CrashGpuWaitBit31Mask << " branch="
            << ((((m_gpuWaitTrace.initialStatus ^ m_gpuWaitTrace.compareStatus) &
                  CrashGpuWaitBit31Mask) != 0)
                    ? "exit_wait"
                    : "continue_wait");
        break;
    case CrashGpuWaitLoopBranchPc:
        if (!m_gpuWaitTrace.hasInitialStatus || !m_gpuWaitTrace.hasLoopStatus)
        {
            msg << "event=field_flip_loop pc=0x" << std::hex << pc << " status=missing";
            break;
        }
        msg << "event=field_flip_loop pc=0x" << std::hex << pc << " initial=0x"
            << m_gpuWaitTrace.initialStatus << " current=0x" << m_gpuWaitTrace.loopStatus
            << " xor=0x" << (m_gpuWaitTrace.initialStatus ^ m_gpuWaitTrace.loopStatus)
            << " mask=0x" << CrashGpuWaitBit31Mask << " branch="
            << ((((m_gpuWaitTrace.initialStatus ^ m_gpuWaitTrace.loopStatus) &
                  CrashGpuWaitBit31Mask) == 0)
                    ? "stay_loop"
                    : "exit_loop");
        break;
    default:
        return;
    }

    m_logger.log(LogLevel::Info, "gpu_wait", msg.str());
}

} // namespace runtime
} // namespace psxrecomp