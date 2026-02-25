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

const char* interruptLineName(InterruptLine line)
{
    switch (line)
    {
    case InterruptLine::VBlank:
        return "VBlank";
    case InterruptLine::Gpu:
        return "Gpu";
    case InterruptLine::Cdrom:
        return "Cdrom";
    case InterruptLine::Dma:
        return "Dma";
    case InterruptLine::Timer0:
        return "Timer0";
    case InterruptLine::Timer1:
        return "Timer1";
    case InterruptLine::Timer2:
        return "Timer2";
    case InterruptLine::Controller:
        return "Controller";
    case InterruptLine::Sio:
        return "Sio";
    case InterruptLine::Spu:
        return "Spu";
    case InterruptLine::Pio:
        return "Pio";
    default:
        return "Unknown";
    }
}

std::string formatPendingLineOrder(u32 pendingMasked)
{
    constexpr InterruptLine kPriorityOrder[] = {
        InterruptLine::VBlank, InterruptLine::Gpu,        InterruptLine::Cdrom,
        InterruptLine::Dma,    InterruptLine::Timer0,     InterruptLine::Timer1,
        InterruptLine::Timer2, InterruptLine::Controller, InterruptLine::Sio,
        InterruptLine::Spu,    InterruptLine::Pio,
    };

    std::ostringstream out;
    bool first = true;
    for (InterruptLine line : kPriorityOrder)
    {
        const u32 bit = static_cast<u32>(line);
        if ((pendingMasked & bit) == 0)
        {
            continue;
        }
        if (!first)
        {
            out << ",";
        }
        out << interruptLineName(line);
        first = false;
    }

    return out.str();
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
    m_criticalSectionDepth = 0;
    m_hookEntryInt = {};
    m_callbackInvoker = CallbackInvoker{};
    m_inHookEntryIntHandler = false;
    m_inCallbackInvocation = false;
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

    m_logger.log(LogLevel::Info, "system", "Runtime reset complete");
}

void PsxSystem::boot()
{
    // Emulate the real PSX BIOS boot sequence: the kernel enables VBlank
    // and timer interrupts in I_MASK before calling the game's entry point.
    // Without this, serviceInterrupts() will never see pending IRQs and
    // VSync/timer callbacks will not fire.
    const u32 bootMask =
        static_cast<u32>(InterruptLine::VBlank) | static_cast<u32>(InterruptLine::Timer0) |
        static_cast<u32>(InterruptLine::Timer1) | static_cast<u32>(InterruptLine::Timer2);
    m_interrupts.writeMask(bootMask);

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

KernelEventTable& PsxSystem::events()
{
    return m_events;
}

InterruptDispatcher& PsxSystem::dispatcher()
{
    return m_dispatcher;
}

void PsxSystem::setCallbackInvoker(CallbackInvoker invoker)
{
    // Store the raw invoker bridge (calls into the generated module).
    m_callbackInvoker = std::move(invoker);

    // The dispatcher should invoke callbacks through the system so that
    // ReturnFromException works (requires m_inCallbackInvocation=true).
    m_dispatcher.setCallbackInvoker([this](u32 address) -> u32
                                    { return this->invokeCallbackRaw(address); });
}

void PsxSystem::serviceInterrupts()
{
    syncLevelInterruptSources();

    u32 pendingMasked = m_interrupts.readStatus() & m_interrupts.readMask();
    if (traceIrqFlowEnabled())
    {
        std::ostringstream msg;
        msg << "event=service_interrupts pending_masked=0x" << std::hex << pendingMasked
            << " status=0x" << m_interrupts.readStatus() << " mask=0x" << m_interrupts.readMask()
            << " critical_depth=" << std::dec << m_criticalSectionDepth
            << " in_callback=" << (m_inCallbackInvocation ? 1 : 0);
        m_logger.log(LogLevel::Info, "irq_trace", msg.str());
    }
    if (pendingMasked == 0)
    {
        // Still allow the event dispatcher to flush deferred callbacks.
        try
        {
            m_dispatcher.serviceInterrupts(m_interrupts, m_events, m_criticalSectionDepth,
                                           &m_logger);
        }
        catch (const ReturnFromExceptionSignal&)
        {
            if (traceIrqFlowEnabled())
            {
                m_logger.log(LogLevel::Info, "irq_trace",
                             "event=return_from_exception source=dispatcher_empty_pending");
            }
            return;
        }
        return;
    }
    // HookEntryInt descriptor callback runs while IRQ status bits are visible.
    if (pendingMasked != 0 && m_criticalSectionDepth == 0 &&
        m_hookEntryInt.descriptorAddress != 0 && !m_inHookEntryIntHandler)
    {
        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=hook_entry_int_invoke descriptor=0x" << std::hex
                << m_hookEntryInt.descriptorAddress;
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        try
        {
            invokeHookEntryIntHandler();
        }
        catch (const ReturnFromExceptionSignal&)
        {
            if (traceIrqFlowEnabled())
            {
                m_logger.log(LogLevel::Info, "irq_trace",
                             "event=return_from_exception source=hook_entry_int");
            }
            return;
        }
    }

    // The hook callback may acknowledge IRQ bits. Recompute pending state
    // before running priority chains so we don't act on stale masks.
    pendingMasked = m_interrupts.readStatus() & m_interrupts.readMask();
    if (pendingMasked != 0 && traceIrqFlowEnabled())
    {
        std::ostringstream msg;
        msg << "event=irq_dispatch_order source=service_interrupts pending_masked=0x" << std::hex
            << pendingMasked << " lines=" << formatPendingLineOrder(pendingMasked);
        m_logger.log(LogLevel::Info, "irq_trace", msg.str());
    }

    // BIOS-style exception handler priority chains (installed via SysEnqIntRP).
    // These handlers are responsible for updating SDK counters (eg. PSn00bSDK VSync)
    // and for acknowledging IRQ sources.
    if (pendingMasked != 0 && dispatchIrqChains())
    {
        if (traceIrqFlowEnabled())
        {
            m_logger.log(LogLevel::Info, "irq_trace",
                         "event=return_from_exception source=irq_chain_dispatch");
        }
        return;
    }

    // Kernel event delivery (OpenEvent/EnableEvent model).
    // A callback may execute ReturnFromException to abort further handling.
    try
    {
        m_dispatcher.serviceInterrupts(m_interrupts, m_events, m_criticalSectionDepth, &m_logger);
    }
    catch (const ReturnFromExceptionSignal&)
    {
        if (traceIrqFlowEnabled())
        {
            m_logger.log(LogLevel::Info, "irq_trace",
                         "event=return_from_exception source=dispatcher_pending");
        }
        return;
    }
}

u32 PsxSystem::resolveHookEntryIntCallback(u32 descriptorAddress) const
{
    if (descriptorAddress == 0)
    {
        return 0;
    }

    const Address descriptorPhysical = normalizeAddress(descriptorAddress);
    if (descriptorPhysical > MemoryMap::RAM_SIZE - sizeof(u32) || (descriptorPhysical & 0x3u) != 0u)
    {
        return 0;
    }

    const u32 callbackAddress =
        readFromRegion<u32>(m_ram.data(), descriptorPhysical, MemoryMap::RAM_SIZE);
    const Address callbackPhysical = normalizeAddress(callbackAddress);
    if ((callbackAddress & 0xE0000000u) != 0x80000000u ||
        callbackPhysical > MemoryMap::RAM_SIZE - sizeof(u32) || (callbackPhysical & 0x3u) != 0u)
    {
        return 0;
    }
    return callbackAddress;
}

void PsxSystem::invokeHookEntryIntHandler()
{
    m_inHookEntryIntHandler = true;
    try
    {
        (void)invokeCallbackRaw(resolveHookEntryIntCallback(m_hookEntryInt.descriptorAddress));
    }
    catch (...)
    {
        m_inHookEntryIntHandler = false;
        throw;
    }
    m_inHookEntryIntHandler = false;
}

void PsxSystem::invokeCallback(u32 address)
{
    if (address == 0)
    {
        return;
    }
    if (m_callbackInvoker)
    {
        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=callback_invoke mode=void addr=0x" << std::hex << address << " pc=0x"
                << m_debugOverlay.lastProgramCounter();
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        const bool previousInCallbackInvocation = m_inCallbackInvocation;
        m_inCallbackInvocation = true;
        try
        {
            (void)m_callbackInvoker(address);
        }
        catch (const ReturnFromExceptionSignal&)
        {
            if (traceIrqFlowEnabled())
            {
                std::ostringstream msg;
                msg << "event=return_from_exception source=callback_invoke addr=0x" << std::hex
                    << address;
                m_logger.log(LogLevel::Info, "irq_trace", msg.str());
            }
            m_inCallbackInvocation = previousInCallbackInvocation;
            return;
        }
        catch (...)
        {
            m_inCallbackInvocation = previousInCallbackInvocation;
            throw;
        }
        m_inCallbackInvocation = previousInCallbackInvocation;
    }
}

u32 PsxSystem::invokeCallbackRaw(u32 address)
{
    if (address == 0)
    {
        return 0;
    }
    if (!m_callbackInvoker)
    {
        return 0;
    }

    const bool previousInCallbackInvocation = m_inCallbackInvocation;
    m_inCallbackInvocation = true;
    try
    {
        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=callback_invoke mode=raw addr=0x" << std::hex << address << " pc=0x"
                << m_debugOverlay.lastProgramCounter();
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        const u32 result = m_callbackInvoker(address);
        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=callback_return mode=raw addr=0x" << std::hex << address << " v0=0x"
                << result;
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        m_inCallbackInvocation = previousInCallbackInvocation;
        return result;
    }
    catch (...)
    {
        m_inCallbackInvocation = previousInCallbackInvocation;
        throw;
    }
}

bool PsxSystem::dispatchIrqChains()
{
    // Skip delivery inside critical sections.
    if (m_criticalSectionDepth > 0)
    {
        return false;
    }
    if (!m_callbackInvoker)
    {
        return false;
    }

    constexpr int kMaxNodesPerChain = 64;
    for (u32 prio = 0; prio < m_irqChainHeads.size(); ++prio)
    {
        u32 node = m_irqChainHeads[prio];
        for (int safety = 0; node != 0 && safety < kMaxNodesPerChain; ++safety)
        {
            const u32 func2 = read<u32>(node + 0x04);
            const u32 func1 = read<u32>(node + 0x08);
            if (traceIrqFlowEnabled())
            {
                std::ostringstream msg;
                msg << "event=irq_dispatch_order source=irq_chain prio=" << std::dec << prio
                    << " index=" << safety << " node=0x" << std::hex << node << " func1=0x" << func1
                    << " func2=0x" << func2;
                m_logger.log(LogLevel::Info, "irq_trace", msg.str());
            }

            if (func1 != 0)
            {
                u32 func1Result = 0;
                try
                {
                    func1Result = invokeCallbackRaw(func1);
                }
                catch (const ReturnFromExceptionSignal&)
                {
                    if (traceIrqFlowEnabled())
                    {
                        m_logger.log(LogLevel::Info, "irq_trace",
                                     "event=return_from_exception source=irq_chain_func1");
                    }
                    return true;
                }

                if (func1Result != 0 && func2 != 0)
                {
                    try
                    {
                        (void)invokeCallbackRaw(func2);
                    }
                    catch (const ReturnFromExceptionSignal&)
                    {
                        if (traceIrqFlowEnabled())
                        {
                            m_logger.log(LogLevel::Info, "irq_trace",
                                         "event=return_from_exception source=irq_chain_func2");
                        }
                        return true;
                    }
                }
            }

            node = read<u32>(node + 0x00);
        }
    }
    return false;
}

u32 PsxSystem::criticalSectionDepth() const
{
    return m_criticalSectionDepth;
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
