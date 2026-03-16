#include "psxrecomp/runtime/psx_system.h"

#include "irq_trace_utils.h"

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
constexpr u32 DISPLAY_LINES_PER_FRAME = 263u;
constexpr u32 DISPLAY_LINE_CYCLES = CYCLES_PER_FRAME / DISPLAY_LINES_PER_FRAME;
constexpr u32 DISPLAY_LINE_CYCLE_REMAINDER = CYCLES_PER_FRAME % DISPLAY_LINES_PER_FRAME;
constexpr u32 BIOS_VECTOR_TABLE_POINTER_OFFSET = 24u;
constexpr std::array<u16, 5> BIOS_CDROM_EVENT_SPECS = {
    0x0010u, // PSX-SPX: BIOS _96_init opens F0000003 specs 10h,20h,40h,80h,8000h
    0x0020u, 0x0040u, 0x0080u, 0x8000u,
};

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

} // namespace

PsxSystem::PsxSystem()
    : m_ram(MemoryMap::RAM_SIZE), m_scratchpad(MemoryMap::SCRATCHPAD_SIZE),
      m_bios(MemoryMap::BIOS_SIZE)
{
    m_stallClassifier.attachSystem(this);
    bindGteRuntimeHooks();
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
    m_mdec.reset();
    m_mdec.setLogCallback(
        [this](LogLevel level, const std::string& category, const std::string& message)
        { m_logger.log(level, category, message); });
    m_cdrom.setDiscBackend(m_disc.get());
    m_cdrom.setXaAudioSink([this](const std::vector<int16_t>& interleavedStereoPcm)
                           { m_spu.pushCdAudioSamples(interleavedStereoPcm); });
    m_input.reset();
    m_sio0.reset();
    m_sio0.setInputController(&m_input);
    m_sio0.setScheduler(&m_scheduler);
    m_sio0.setIrqCallback(
        [this]()
        {
            m_interrupts.raise(InterruptLine::Controller);
            syncCop0InterruptPending();
            m_debugOverlay.incrementInterruptsRaised();
        });
    m_dma.reset();
    m_interrupts.reset();
    m_events.reset();
    m_dispatcher.reset();
    m_scheduler.reset();
    m_debugOverlay.reset();
    m_timers.reset();
    m_cop0.reset();
    m_gte.reset();
    m_stallClassifier.reset();
    m_callbackTrace.reset();
    m_hookEntryIntTrace.reset();
    bindGteRuntimeHooks();
    m_criticalSectionDepth = 0;
    m_hookEntryInt = {};
    resetBiosCdromState();
    m_biosFt.reset();
    m_biosFt.setDisc(m_disc.get());
    m_callbackInvoker = CallbackInvoker{};
    m_inHookEntryIntHandler = false;
    m_inCallbackInvocation = false;
    m_hasPendingCallbackRegisters = false;
    m_callbackContextCommitGeneration = 0;
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
    m_pendingSpuDmaCompletionCycles = 0;
    m_pendingSpuDmaCompletion = false;
    m_cpuCycles = 0;
    m_gpuDrainCarry = 0;
    m_videoSchedulePrimed = false;
    m_videoLineScheduleCarry = 0;
    primeVideoSchedule();
    syncCop0InterruptPending();

    m_logger.log(LogLevel::Info, "system", "Runtime reset complete");
}

void PsxSystem::resetBiosCdromState()
{
    for (u32 handle : m_biosCdrom.eventHandles)
    {
        if (handle != 0 && handle != 0xFFFFFFFFu)
        {
            m_events.closeEvent(handle);
        }
    }
    m_biosCdrom = {};
}

void PsxSystem::initializeBiosCdromState(u32 handleStorageAddress)
{
    resetBiosCdromState();
    m_biosCdrom.initialized = true;
    m_biosCdrom.handleStorageAddress = handleStorageAddress;
    const Address handleStoragePhysical = normalizeAddress(handleStorageAddress);
    const bool hasHandleStorage =
        handleStorageAddress != 0u &&
        isMainRamAddress(handleStoragePhysical,
                         static_cast<Address>(BIOS_CDROM_EVENT_SPECS.size() * sizeof(u32))) &&
        foldMainRamAddress(handleStoragePhysical) <=
            MemoryMap::RAM_SIZE - static_cast<Address>(BIOS_CDROM_EVENT_SPECS.size() * sizeof(u32));

    if (hasHandleStorage)
    {
        for (size_t i = 0; i < BIOS_CDROM_EVENT_SPECS.size(); ++i)
        {
            write<u32>(handleStorageAddress + static_cast<u32>(i * sizeof(u32)), 0u);
        }
    }

    // PSX-SPX: the BIOS opens five internal CDROM events for class F0000003
    // during _96_init, and the kernel performs that setup before the boot
    // executable starts running.
    for (size_t i = 0; i < BIOS_CDROM_EVENT_SPECS.size(); ++i)
    {
        const u32 handle = m_events.openEvent(EventClass::Cdrom, BIOS_CDROM_EVENT_SPECS[i],
                                              EventMode::NoCallback, 0);
        m_biosCdrom.eventHandles[i] = handle;
        if (hasHandleStorage)
        {
            write<u32>(handleStorageAddress + static_cast<u32>(i * sizeof(u32)), handle);
        }
        if (handle != 0xFFFFFFFFu)
        {
            m_events.enableEvent(handle);
        }
    }

    // PSX-SPX notes the BIOS/libcd path typically enables all CDROM IRQ subtypes.
    m_cdrom.writeInterruptEnable(0x1Fu);
}

void PsxSystem::bindGteRuntimeHooks()
{
    m_gte.setCpuStallCallback(
        [this](u32 cycles)
        {
            if (cycles > 0)
            {
                tickCpuCycles(cycles);
            }
        });
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

    // PSX-SPX: GetC0Table/GetB0Table expose BIOS-owned writable table roots in
    // kernel RAM. Games such as Crash patch the C0 handler table during boot.
    // Keep the layout minimal but non-null so those installs target kernel
    // workspace instead of clobbering address 0.
    for (u32 address = BIOS_C0_TABLE_ADDRESS; address < BIOS_B0_HANDLER_TABLE_ADDRESS + 0x40u;
         address += sizeof(u32))
    {
        write<u32>(address, 0u);
    }
    write<u32>(BIOS_C0_TABLE_ADDRESS + BIOS_VECTOR_TABLE_POINTER_OFFSET,
               BIOS_C0_HANDLER_TABLE_ADDRESS);
    write<u32>(BIOS_B0_TABLE_ADDRESS + BIOS_VECTOR_TABLE_POINTER_OFFSET,
               BIOS_B0_HANDLER_TABLE_ADDRESS);

    m_cdrom.primeBootState(m_disc != nullptr);
    m_spu.primeBootState();

    initializeBiosCdromState(0u);

    m_logger.log(LogLevel::Info, "system", "Runtime boot sequence initialized");

    // Diagnostic: dump the game's interrupt handler table if it resides in
    // the loaded EXE region. Helps verify CDROM handler registration.
    constexpr u32 HandlerTableBase = 0x801654EC;
    constexpr u32 HandlerEntries = 11;
    const Address htPhysical = normalizeAddress(HandlerTableBase);
    if (isMainRamAddress(htPhysical, HandlerEntries * sizeof(u32)))
    {
        std::ostringstream msg;
        msg << "handler_table_dump base=0x" << std::hex << HandlerTableBase;
        for (u32 i = 0; i < HandlerEntries; ++i)
        {
            const u32 addr = HandlerTableBase + i * 4;
            const u32 val = read<u32>(addr);
            msg << " [" << std::dec << i << "]=0x" << std::hex << val;
        }
        m_logger.log(LogLevel::Info, "boot_diag", msg.str());
    }

    // Diagnostic: dump CD driver hardware pointers from EXE data section
    {
        std::ostringstream msg;
        msg << "cd_hw_ptrs";
        constexpr u32 addrs[] = {0x80163CEC, 0x80163CF0, 0x80163CF4, 0x80163D18, 0x8016531C};
        const char* names[] = {"D3_MADR_ptr", "D3_BCR_ptr", "D3_CHCR_ptr", "DICR_ptr",
                               "CD_STATUS_ptr"};
        for (int i = 0; i < 5; ++i)
        {
            const u32 val = read<u32>(addrs[i]);
            msg << " " << names[i] << "=0x" << std::hex << val;
        }
        m_logger.log(LogLevel::Info, "boot_diag", msg.str());
    }
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
    if (m_pendingSpuDmaCompletion)
    {
        if (cpuCycles >= m_pendingSpuDmaCompletionCycles)
        {
            m_pendingSpuDmaCompletion = false;
            m_pendingSpuDmaCompletionCycles = 0;
            auto callbacks = m_events.deliverByClassSpec(EventClass::Spu, EventSpec::CommandDone);
            for (u32 address : callbacks)
            {
                invokeCallback(address);
            }
        }
        else
        {
            m_pendingSpuDmaCompletionCycles -= cpuCycles;
        }
    }
    m_cdrom.tick(cpuCycles);
    m_gte.tickCpuCycles(cpuCycles);
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
    const bool cdromIrq = m_cdrom.hasIrqRequest();
    raiseIfRequested(cdromIrq, InterruptLine::Cdrom);
    if (cdromIrq)
    {
        static int sCdromIrqCount = 0;
        if (sCdromIrqCount < 5)
        {
            std::fprintf(
                stderr,
                "[diag] syncLevel cdromIrq=true IF=0x%02X IE=0x%02X I_STAT=0x%04X I_MASK=0x%04X\n",
                m_cdrom.readInterruptFlags(), m_cdrom.readInterruptEnable(),
                m_interrupts.readStatus(), m_interrupts.readMask());
            ++sCdromIrqCount;
        }
    }
    raiseIfRequested(m_spu.hasIrqRequest(), InterruptLine::Spu);
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
    u32 lineCycles = DISPLAY_LINE_CYCLES;
    m_videoLineScheduleCarry += DISPLAY_LINE_CYCLE_REMAINDER;
    if (m_videoLineScheduleCarry >= DISPLAY_LINES_PER_FRAME)
    {
        lineCycles += 1;
        m_videoLineScheduleCarry -= DISPLAY_LINES_PER_FRAME;
    }
    m_scheduler.schedule(lineCycles, [this]() { handleDisplayLineTick(); });
}

void PsxSystem::handleDisplayLineTick()
{
    const Gpu::DisplayPhase previousPhase = m_gpu.displayPhase();

    m_gpu.tickDisplayLine();
    m_timers.tickDisplayLine(
        [this](InterruptLine line)
        {
            m_interrupts.raise(line);
            syncCop0InterruptPending();
            m_debugOverlay.incrementInterruptsRaised();
        });

    if (previousPhase != Gpu::DisplayPhase::VBlankStart &&
        m_gpu.displayPhase() == Gpu::DisplayPhase::VBlankStart)
    {
        m_interrupts.raise(InterruptLine::VBlank);
        // Keep Cause.IP2 synchronized even before the next serviceInterrupts() call.
        syncCop0InterruptPending();
        m_debugOverlay.incrementInterruptsRaised();
        m_debugOverlay.setLastFrameCycles(CYCLES_PER_FRAME);
        m_logger.log(LogLevel::Debug, "perf", m_debugOverlay.renderText());
        ++m_frameCount;
    }

    u32 lineCycles = DISPLAY_LINE_CYCLES;
    m_videoLineScheduleCarry += DISPLAY_LINE_CYCLE_REMAINDER;
    if (m_videoLineScheduleCarry >= DISPLAY_LINES_PER_FRAME)
    {
        lineCycles += 1;
        m_videoLineScheduleCarry -= DISPLAY_LINES_PER_FRAME;
    }
    m_scheduler.schedule(lineCycles, [this]() { handleDisplayLineTick(); });
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

} // namespace runtime
} // namespace psxrecomp
