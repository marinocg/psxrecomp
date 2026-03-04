#include "psxrecomp/runtime/psx_system.h"

#include <cstdlib>
#include <sstream>
#include <utility>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u32 CYCLES_PER_FRAME = 564480;
constexpr u32 GPU_FIFO_DRAIN_CYCLES_PER_FRAME = 64u * 2u;
constexpr u32 VBLANK_CYCLES = CYCLES_PER_FRAME / 10u;
constexpr u32 VBLANK_MID_CYCLES = VBLANK_CYCLES / 2u;
constexpr u32 ACTIVE_CYCLES = CYCLES_PER_FRAME - VBLANK_CYCLES;

bool traceIrqFlowEnabled()
{
    if (const char* env = std::getenv("PSXRECOMP_TRACE_IRQ_FLOW"))
    {
        return env[0] == '1';
    }
    return false;
}

const char* interruptTraceKindName(InterruptController::TraceEvent::Kind kind)
{
    switch (kind)
    {
    case InterruptController::TraceEvent::Kind::Reset:
        return "reset";
    case InterruptController::TraceEvent::Kind::WriteStatus:
        return "write_status";
    case InterruptController::TraceEvent::Kind::WriteMask:
        return "write_mask";
    case InterruptController::TraceEvent::Kind::Raise:
        return "raise";
    case InterruptController::TraceEvent::Kind::RestoreState:
        return "restore_state";
    default:
        return "unknown";
    }
}

void appendU32(std::vector<u8>& out, u32 value)
{
    out.push_back(static_cast<u8>(value & 0xFF));
    out.push_back(static_cast<u8>((value >> 8) & 0xFF));
    out.push_back(static_cast<u8>((value >> 16) & 0xFF));
    out.push_back(static_cast<u8>((value >> 24) & 0xFF));
}

} // namespace

PsxSystem::PsxSystem()
    : m_ram(MemoryMap::RAM_SIZE), m_scratchpad(MemoryMap::SCRATCHPAD_SIZE),
      m_bios(MemoryMap::BIOS_SIZE)
{
}

PsxSystem::~PsxSystem() = default;

bool PsxSystem::initialize()
{
    reset();
    boot();
    return !m_ram.empty() && !m_scratchpad.empty() && !m_bios.empty();
}

void PsxSystem::reset()
{
    if (!m_ram.empty())
    {
        std::memset(m_ram.data(), 0, MemoryMap::RAM_SIZE);
    }
    if (!m_scratchpad.empty())
    {
        std::memset(m_scratchpad.data(), 0, MemoryMap::SCRATCHPAD_SIZE);
    }
    if (!m_bios.empty())
    {
        std::memset(m_bios.data(), 0, MemoryMap::BIOS_SIZE);
    }

    m_gpu.reset();
    m_spu.reset();
    m_cdrom.reset();
    m_input.reset();
    m_dma.reset();
    m_interrupts.reset();
    m_events.reset();
    m_dispatcher.reset();
    m_scheduler.reset();
    m_debugOverlay.reset();
    m_timers.reset();
    m_cop0.reset();
    m_criticalSectionDepth = 0;
    m_hookEntryInt = {};
    m_callbackInvoker = CallbackInvoker{};
    m_inHookEntryIntHandler = false;
    m_inCallbackInvocation = false;
    m_hasPendingCallbackRegisters = false;
    m_pendingCallbackRegisters = {};
    m_pendingCallbackRegisterMask.fill(false);
    if (traceIrqFlowEnabled())
    {
        m_interrupts.setTraceHook(
            [this](const InterruptController::TraceEvent& event)
            {
                std::ostringstream msg;
                msg << "event=irq_state kind=" << interruptTraceKindName(event.kind) << " value=0x"
                    << std::hex << event.value << " status_before=0x" << event.statusBefore
                    << " status_after=0x" << event.statusAfter << " mask_before=0x"
                    << event.maskBefore << " mask_after=0x" << event.maskAfter << " pc=0x"
                    << m_debugOverlay.lastProgramCounter();
                m_logger.log(LogLevel::Info, "irq_trace", msg.str());
            });
    }
    else
    {
        m_interrupts.setTraceHook(InterruptController::TraceHook{});
    }
    m_irqChainHeads = {};
    m_frameCount = 0;
    m_cpuCycles = 0;
    m_gpuDrainCarry = 0;
    m_videoSchedulePrimed = false;
    primeVideoSchedule();
    syncCop0InterruptPending();

    m_logger.log(LogLevel::Info, "system", "Runtime reset complete");
}

void PsxSystem::boot()
{
    // Emulate BIOS-ready COP0 defaults before handing control to the game:
    // IEc=1 and IM2=1 (mask for Cause.IP2 / IRQ controller line), while
    // remaining in kernel mode.
    constexpr u32 StatusIEcBit = 1u << 0;
    constexpr u32 StatusKUcBit = 1u << 1;
    constexpr u32 StatusIM2Bit = 1u << 10;
    const u32 statusBefore = m_cop0.mfc0(Cop0::RegisterIndex::Status);
    const u32 bootStatus = (statusBefore & ~StatusKUcBit) | StatusIEcBit | StatusIM2Bit;
    m_cop0.mtc0(Cop0::RegisterIndex::Status, bootStatus);

    // Emulate the real PSX BIOS boot sequence: the kernel enables VBlank
    // and timer interrupts in I_MASK before calling the game's entry point.
    // Without this, serviceInterrupts() will never see pending IRQs and
    // VSync/timer callbacks will not fire.
    const u32 bootMask =
        static_cast<u32>(InterruptLine::VBlank) | static_cast<u32>(InterruptLine::Timer0) |
        static_cast<u32>(InterruptLine::Timer1) | static_cast<u32>(InterruptLine::Timer2);
    m_interrupts.writeMask(bootMask);
    syncCop0InterruptPending();

    m_logger.log(LogLevel::Info, "system", "Runtime boot sequence initialized");
}

void PsxSystem::runFrame()
{
    tickCpuCycles(CYCLES_PER_FRAME);
}

void PsxSystem::tickCpuCycles(u32 cpuCycles)
{
    if (cpuCycles == 0)
    {
        return;
    }

    if (!m_videoSchedulePrimed)
    {
        primeVideoSchedule();
    }

    m_cpuCycles += cpuCycles;
    m_gpuDrainCarry += static_cast<uint64_t>(cpuCycles) * GPU_FIFO_DRAIN_CYCLES_PER_FRAME;
    const u32 gpuDrainCycles = static_cast<u32>(m_gpuDrainCarry / CYCLES_PER_FRAME);
    m_gpuDrainCarry %= CYCLES_PER_FRAME;
    if (gpuDrainCycles > 0)
    {
        m_gpu.tickGpu(gpuDrainCycles);
    }

    m_spu.tick(cpuCycles);
    m_cdrom.tick(cpuCycles);
    m_timers.tick(cpuCycles,
                  [this](InterruptLine line)
                  {
                      m_interrupts.raise(line);
                      syncCop0InterruptPending();
                      m_debugOverlay.incrementInterruptsRaised();
                  });
    syncLevelInterruptSources();
    m_scheduler.tick(cpuCycles);
}

void PsxSystem::syncLevelInterruptSources()
{
    const auto raiseIfRequested = [this](bool requested, InterruptLine line)
    {
        if (!requested)
        {
            return;
        }

        const u32 lineBit = static_cast<u32>(line);
        if ((m_interrupts.readStatus() & lineBit) == 0)
        {
            m_interrupts.raise(line);
            m_debugOverlay.incrementInterruptsRaised();
        }
    };

    raiseIfRequested(m_gpu.irqPending(), InterruptLine::Gpu);
    raiseIfRequested(m_cdrom.hasIrqRequest(), InterruptLine::Cdrom);

    raiseIfRequested(m_dma.irqRequested(), InterruptLine::Dma);
    syncCop0InterruptPending();
}

void PsxSystem::syncCop0InterruptPending()
{
    // PSX interrupt controller output is routed to CPU interrupt line IP2.
    // Mirror I_STAT&I_MASK aggregate state into Cause.IP2.
    m_cop0.setHardwareInterruptPending(m_interrupts.isInterruptPending());
}

uint64_t PsxSystem::cpuCyclesElapsed() const
{
    return m_cpuCycles;
}

void PsxSystem::primeVideoSchedule()
{
    if (m_videoSchedulePrimed)
    {
        return;
    }
    m_videoSchedulePrimed = true;
    m_scheduler.schedule(ACTIVE_CYCLES, [this]() { handleVBlankStart(); });
}

void PsxSystem::handleVBlankStart()
{
    m_gpu.tickDisplayLine();
    m_interrupts.raise(InterruptLine::VBlank);
    // Keep Cause.IP2 synchronized even before the next serviceInterrupts() call.
    syncCop0InterruptPending();
    m_debugOverlay.incrementInterruptsRaised();
    m_debugOverlay.setLastFrameCycles(CYCLES_PER_FRAME);
    m_logger.log(LogLevel::Debug, "perf", m_debugOverlay.renderText());
    ++m_frameCount;

    m_scheduler.schedule(VBLANK_MID_CYCLES, [this]() { m_gpu.tickDisplayLine(); });
    m_scheduler.schedule(VBLANK_CYCLES, [this]() { m_gpu.tickDisplayLine(); });
    m_scheduler.schedule(CYCLES_PER_FRAME, [this]() { handleVBlankStart(); });
}

void PsxSystem::callGpuIntrinsic(Address address)
{
    m_logger.log(
        LogLevel::Debug, "intrinsic",
        "GPU intrinsic call at 0x" +
            [&]()
            {
                std::ostringstream stream;
                stream << std::hex << address;
                return stream.str();
            }());
}

void PsxSystem::callSpuIntrinsic(Address address)
{
    m_logger.log(
        LogLevel::Debug, "intrinsic",
        "SPU intrinsic call at 0x" +
            [&]()
            {
                std::ostringstream stream;
                stream << std::hex << address;
                return stream.str();
            }());
}

void PsxSystem::callCdromIntrinsic(Address address)
{
    m_logger.log(
        LogLevel::Debug, "intrinsic",
        "CDROM intrinsic call at 0x" +
            [&]()
            {
                std::ostringstream stream;
                stream << std::hex << address;
                return stream.str();
            }());
}

u32 PsxSystem::frameCount() const
{
    return m_frameCount;
}

u32 PsxSystem::advanceFrame()
{
    tickCpuCycles(CYCLES_PER_FRAME);
    return m_frameCount;
}

u8* PsxSystem::getRam()
{
    return m_ram.data();
}

const u8* PsxSystem::getRam() const
{
    return m_ram.data();
}

Gpu& PsxSystem::gpu()
{
    return m_gpu;
}

Spu& PsxSystem::spu()
{
    return m_spu;
}

Cdrom& PsxSystem::cdrom()
{
    return m_cdrom;
}

InputController& PsxSystem::input()
{
    return m_input;
}

DmaController& PsxSystem::dma()
{
    return m_dma;
}

InterruptController& PsxSystem::interrupts()
{
    return m_interrupts;
}

Scheduler& PsxSystem::scheduler()
{
    return m_scheduler;
}

RuntimeLogger& PsxSystem::logger()
{
    return m_logger;
}

RuntimeDebugOverlay& PsxSystem::debugOverlay()
{
    return m_debugOverlay;
}

TimerController& PsxSystem::timers()
{
    return m_timers;
}

Cop0& PsxSystem::cop0()
{
    return m_cop0;
}

KernelEventTable& PsxSystem::events()
{
    return m_events;
}

InterruptDispatcher& PsxSystem::dispatcher()
{
    return m_dispatcher;
}

void PsxSystem::setDiscSwapInfo(DiscSwapInfo info)
{
    m_discSwapInfo = std::move(info);
}

const PsxSystem::DiscSwapInfo& PsxSystem::discSwapInfo() const
{
    return m_discSwapInfo;
}

std::vector<u8> PsxSystem::dumpRam() const
{
    return m_ram;
}

std::vector<u8> PsxSystem::dumpVram() const
{
    std::vector<u8> bytes;
    const auto& words = m_gpu.vramWords();
    bytes.reserve(words.size() * sizeof(u32));
    for (u32 value : words)
    {
        appendU32(bytes, value);
    }
    return bytes;
}

std::vector<u8> PsxSystem::dumpSpuRam() const
{
    std::vector<u8> bytes;
    const auto& words = m_spu.ramWords();
    bytes.reserve(words.size() * sizeof(u32));
    for (u32 value : words)
    {
        appendU32(bytes, value);
    }
    return bytes;
}

} // namespace runtime
} // namespace psxrecomp
