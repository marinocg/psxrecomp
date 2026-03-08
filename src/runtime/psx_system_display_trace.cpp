#include "psxrecomp/runtime/psx_system.h"

#include <cstdlib>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr Address CrashGpuWaitEntryPc = 0x8003E4F0u;
constexpr Address CrashGpuWaitEndPc = 0x8003E620u;
constexpr Address CrashDisplayCompareStartPc = 0x80045688u;
constexpr Address CrashDisplayCompareEndPc = 0x80045748u;

constexpr Address CrashGpuWaitLastCounterGlobal = 0x8005392Cu;
constexpr Address CrashGpuWaitLastReturnGlobal = 0x80053930u;
constexpr Address CrashGpuWaitCachedReturnGlobal = 0x800549F0u;
constexpr Address CrashDisplayThresholdGlobal = 0x800571B0u;
constexpr Address CrashDisplayThresholdCounterGlobal = 0x800571B4u;
constexpr Address CrashDisplayThresholdMessageGlobal = 0x800571B8u;

constexpr u32 CrashGpuWaitBit19Mask = 0x00080000u;
constexpr u32 CrashGpuWaitBit31Mask = 0x80000000u;

bool envFlagEnabled(const char* name)
{
    if (const char* env = std::getenv(name))
    {
        return env[0] == '1';
    }
    return false;
}

bool isWithinInclusive(Address pc, Address start, Address end)
{
    return pc >= start && pc <= end;
}
} // namespace

bool PsxSystem::displayTimingTraceEnabled()
{
    if (!m_displayTimingTrace.configured)
    {
        m_displayTimingTrace.enabled = envFlagEnabled("PSXRECOMP_TRACE_DISPLAY_TIMING");
        m_displayTimingTrace.configured = true;
    }
    return m_displayTimingTrace.enabled;
}

const char* PsxSystem::displayPhaseName(Gpu::DisplayPhase phase)
{
    switch (phase)
    {
    case Gpu::DisplayPhase::ActiveDisplay:
        return "active";
    case Gpu::DisplayPhase::VBlankStart:
        return "vblank_start";
    case Gpu::DisplayPhase::VBlankEnd:
        return "vblank_end";
    default:
        return "unknown";
    }
}

void PsxSystem::traceDisplayTimingProgramCounter(Address pc)
{
    if (!displayTimingTraceEnabled())
    {
        return;
    }

    if (isWithinInclusive(pc, CrashGpuWaitEntryPc, CrashGpuWaitEndPc))
    {
        traceDisplayTimingSnapshot("gpu_wait_helper", pc);
        return;
    }

    if (isWithinInclusive(pc, CrashDisplayCompareStartPc, CrashDisplayCompareEndPc))
    {
        traceDisplayTimingSnapshot("display_compare", pc);
    }
}

void PsxSystem::traceDisplayTimingSnapshot(const char* source, Address pc)
{
    if (!displayTimingTraceEnabled())
    {
        return;
    }

    const u32 gpuStatus = m_gpu.readStatus();
    const u32 timer1Counter = static_cast<u32>(m_timers.readCounter(1));
    const u32 displayLine = static_cast<u32>(m_gpu.displayLine());
    const Gpu::DisplayPhase displayPhase = m_gpu.displayPhase();
    const bool oddField = m_gpu.oddField();
    const u32 helperLastCounter = read<u32>(CrashGpuWaitLastCounterGlobal);
    const u32 helperLastReturn = read<u32>(CrashGpuWaitLastReturnGlobal);
    const u32 helperCachedReturn = read<u32>(CrashGpuWaitCachedReturnGlobal);
    const u32 threshold = read<u32>(CrashDisplayThresholdGlobal);
    const u32 thresholdCounter = read<u32>(CrashDisplayThresholdCounterGlobal);
    const u32 thresholdMessage = read<u32>(CrashDisplayThresholdMessageGlobal);
    const bool thresholdExceeded = threshold < helperCachedReturn;

    std::ostringstream msg;
    bool changed = false;
    msg << "event=state source=" << source << " pc=0x" << std::hex << pc;

    if (!m_displayTimingTrace.hasGpuStatus || m_displayTimingTrace.gpuStatus != gpuStatus)
    {
        m_displayTimingTrace.hasGpuStatus = true;
        m_displayTimingTrace.gpuStatus = gpuStatus;
        msg << " gpustat=0x" << gpuStatus << std::dec
            << " bit19=" << ((gpuStatus & CrashGpuWaitBit19Mask) != 0)
            << " bit31=" << ((gpuStatus & CrashGpuWaitBit31Mask) != 0);
        changed = true;
    }

    if (!m_displayTimingTrace.hasTimer1Counter ||
        m_displayTimingTrace.timer1Counter != timer1Counter)
    {
        m_displayTimingTrace.hasTimer1Counter = true;
        m_displayTimingTrace.timer1Counter = timer1Counter;
        msg << " timer1=" << timer1Counter;
        changed = true;
    }

    if (!m_displayTimingTrace.hasDisplayLine || m_displayTimingTrace.displayLine != displayLine ||
        m_displayTimingTrace.displayPhase != displayPhase ||
        m_displayTimingTrace.oddField != oddField)
    {
        m_displayTimingTrace.hasDisplayLine = true;
        m_displayTimingTrace.displayLine = displayLine;
        m_displayTimingTrace.displayPhase = displayPhase;
        m_displayTimingTrace.oddField = oddField;
        msg << " line=" << displayLine << " phase=" << displayPhaseName(displayPhase)
            << " odd_field=" << oddField;
        changed = true;
    }

    if (!m_displayTimingTrace.hasHelperLastCounter ||
        m_displayTimingTrace.helperLastCounter != helperLastCounter)
    {
        m_displayTimingTrace.hasHelperLastCounter = true;
        m_displayTimingTrace.helperLastCounter = helperLastCounter;
        msg << " helper_last_counter=0x" << std::hex << helperLastCounter << std::dec;
        changed = true;
    }

    if (!m_displayTimingTrace.hasHelperLastReturn ||
        m_displayTimingTrace.helperLastReturn != helperLastReturn)
    {
        m_displayTimingTrace.hasHelperLastReturn = true;
        m_displayTimingTrace.helperLastReturn = helperLastReturn;
        msg << " helper_last_return=0x" << std::hex << helperLastReturn << std::dec;
        changed = true;
    }

    if (!m_displayTimingTrace.hasHelperCachedReturn ||
        m_displayTimingTrace.helperCachedReturn != helperCachedReturn)
    {
        m_displayTimingTrace.hasHelperCachedReturn = true;
        m_displayTimingTrace.helperCachedReturn = helperCachedReturn;
        msg << " helper_cached_return=0x" << std::hex << helperCachedReturn << std::dec;
        changed = true;
    }

    if (!m_displayTimingTrace.hasThreshold || m_displayTimingTrace.threshold != threshold)
    {
        m_displayTimingTrace.hasThreshold = true;
        m_displayTimingTrace.threshold = threshold;
        msg << " threshold=0x" << std::hex << threshold << std::dec;
        changed = true;
    }

    if (!m_displayTimingTrace.hasThresholdCounter ||
        m_displayTimingTrace.thresholdCounter != thresholdCounter)
    {
        m_displayTimingTrace.hasThresholdCounter = true;
        m_displayTimingTrace.thresholdCounter = thresholdCounter;
        msg << " threshold_counter=" << thresholdCounter;
        changed = true;
    }

    if (!m_displayTimingTrace.hasThresholdMessage ||
        m_displayTimingTrace.thresholdMessage != thresholdMessage)
    {
        m_displayTimingTrace.hasThresholdMessage = true;
        m_displayTimingTrace.thresholdMessage = thresholdMessage;
        msg << " threshold_message=0x" << std::hex << thresholdMessage << std::dec;
        changed = true;
    }

    if (!m_displayTimingTrace.hasThresholdExceeded ||
        m_displayTimingTrace.thresholdExceeded != thresholdExceeded)
    {
        m_displayTimingTrace.hasThresholdExceeded = true;
        m_displayTimingTrace.thresholdExceeded = thresholdExceeded;
        msg << " threshold_exceeded=" << thresholdExceeded;
        changed = true;
    }

    if (changed)
    {
        m_logger.log(LogLevel::Info, "display_timing", msg.str());
    }
}

void PsxSystem::traceDisplayLineTick(Address pc, u32 callIndex, u16 previousLine,
                                     Gpu::DisplayPhase previousPhase, bool previousOddField)
{
    if (!displayTimingTraceEnabled())
    {
        return;
    }

    ++m_displayTimingTrace.tickDisplayCalls;

    std::ostringstream msg;
    msg << "event=tick_display_line call=" << callIndex << " pc=0x" << std::hex << pc
        << std::dec << " line=" << previousLine << "->" << m_gpu.displayLine() << " phase="
        << displayPhaseName(previousPhase) << "->" << displayPhaseName(m_gpu.displayPhase())
        << " odd_field=" << previousOddField << "->" << m_gpu.oddField() << " gpustat=0x"
        << std::hex << m_gpu.readStatus() << std::dec << " bit19="
        << ((m_gpu.readStatus() & CrashGpuWaitBit19Mask) != 0) << " bit31="
        << ((m_gpu.readStatus() & CrashGpuWaitBit31Mask) != 0);
    m_logger.log(LogLevel::Info, "display_timing", msg.str());

    traceDisplayTimingSnapshot("tick_display_line", pc);
}

} // namespace runtime
} // namespace psxrecomp
