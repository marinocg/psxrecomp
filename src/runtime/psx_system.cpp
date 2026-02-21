#include "psxrecomp/runtime/psx_system.h"

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

std::optional<Address> findLegacyDrawSyncDispatcher(const std::vector<u8>& ram)
{
    // Signature for PSn00bSDK's DrawSync callback dispatcher:
    //   addiu sp,sp,-32
    //   sw    s1,24(sp)
    //   lui   s1,0x8001
    //   sw    s0,20(sp)
    //   lbu   s0,0x55f9(s1)
    constexpr u32 kSig0 = 0x27BDFFE0u;
    constexpr u32 kSig1 = 0xAFB10018u;
    constexpr u32 kSig2 = 0x3C118001u;
    constexpr u32 kSig3 = 0xAFB00014u;
    constexpr u32 kSig4Mask = 0xFFFF0000u;
    constexpr u32 kSig4Value = 0x92300000u;

    for (Address offset = 0; offset <= MemoryMap::RAM_SIZE - sizeof(u32) * 5; offset += 4)
    {
        u32 w0 = 0;
        u32 w1 = 0;
        u32 w2 = 0;
        u32 w3 = 0;
        u32 w4 = 0;
        std::memcpy(&w0, ram.data() + offset + 0, sizeof(u32));
        std::memcpy(&w1, ram.data() + offset + 4, sizeof(u32));
        std::memcpy(&w2, ram.data() + offset + 8, sizeof(u32));
        std::memcpy(&w3, ram.data() + offset + 12, sizeof(u32));
        std::memcpy(&w4, ram.data() + offset + 16, sizeof(u32));
        if (w0 == kSig0 && w1 == kSig1 && w2 == kSig2 && w3 == kSig3 &&
            (w4 & kSig4Mask) == kSig4Value)
        {
            return 0x80000000u | offset;
        }
    }
    return std::nullopt;
}

bool matchesLegacyVblankDispatcherSignature(const std::vector<u8>& ram, Address address)
{
    const Address offset = address & 0x1FFFFFFFu;
    if (offset > MemoryMap::RAM_SIZE - sizeof(u32) * 4)
    {
        return false;
    }

    u32 w0 = 0;
    u32 w1 = 0;
    u32 w2 = 0;
    u32 w3 = 0;
    std::memcpy(&w0, ram.data() + offset + 0, sizeof(u32));
    std::memcpy(&w1, ram.data() + offset + 4, sizeof(u32));
    std::memcpy(&w2, ram.data() + offset + 8, sizeof(u32));
    std::memcpy(&w3, ram.data() + offset + 12, sizeof(u32));

    // PSn00bSDK VBlank dispatcher preamble:
    //   lui v1,0x8001
    //   lw  v0,imm(v1)
    //   lui a0,0x8001
    //   lw  t9,imm(a0)
    return w0 == 0x3C038001u && (w1 & 0xFFFF0000u) == 0x8C620000u && w2 == 0x3C048001u &&
           (w3 & 0xFFFF0000u) == 0x8C990000u;
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
    m_customExitHandler = 0;
    m_callbackInvoker = CallbackInvoker{};
    m_inCustomExitHandler = false;
    m_inCallbackInvocation = false;
    m_legacyDrawSyncDispatcher.reset();
    m_legacyDrawSyncScanDone = false;
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
    if (m_gpu.irqPending() &&
        (m_interrupts.readStatus() & static_cast<u32>(InterruptLine::Gpu)) == 0)
    {
        m_interrupts.raise(InterruptLine::Gpu);
        m_debugOverlay.incrementInterruptsRaised();
    }
    if (m_cdrom.hasIrqRequest() &&
        (m_interrupts.readStatus() & static_cast<u32>(InterruptLine::Cdrom)) == 0)
    {
        m_interrupts.raise(InterruptLine::Cdrom);
        m_debugOverlay.incrementInterruptsRaised();
    }
    m_scheduler.tick(cpuCycles);
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

void PsxSystem::setAutoFrameProgressOnInterruptPoll(bool enabled)
{
    (void)enabled;
}

void PsxSystem::setVsyncCounterAddress(Address address)
{
    (void)address;
}

void PsxSystem::setDrawSyncBusyAddress(Address address)
{
    (void)address;
}

void PsxSystem::clearDrawSyncBusy() {}

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
    m_callbackInvoker = invoker;
    m_dispatcher.setCallbackInvoker(std::move(invoker));
}

void PsxSystem::serviceInterrupts()
{
    if (m_inCallbackInvocation)
    {
        return;
    }

    const u32 pendingMasked = m_interrupts.readStatus() & m_interrupts.readMask();
    // Transitional fallback:
    // Some demos still rely on SDK interrupt callback trampolines that are not yet
    // covered by the C0/B0 interrupt API model in this runtime. Until that model
    // is complete, keep this signature-based callback path to avoid regressing
    // DrawSync/VSync progress.
    if (!m_legacyDrawSyncScanDone)
    {
        m_legacyDrawSyncDispatcher = findLegacyDrawSyncDispatcher(m_ram);
        m_legacyDrawSyncScanDone = true;
    }

    if ((pendingMasked & static_cast<u32>(InterruptLine::VBlank)) != 0 &&
        m_legacyDrawSyncDispatcher.has_value() && *m_legacyDrawSyncDispatcher >= 0x80000030u)
    {
        const Address vblankDispatcher = *m_legacyDrawSyncDispatcher - 0x30u;
        if (matchesLegacyVblankDispatcherSignature(m_ram, vblankDispatcher))
        {
            invokeCallback(vblankDispatcher);
        }
    }

    if ((pendingMasked & static_cast<u32>(InterruptLine::Dma)) != 0)
    {
        if (m_legacyDrawSyncDispatcher.has_value())
        {
            invokeCallback(*m_legacyDrawSyncDispatcher);
        }
    }

    const u32 statusBefore = m_interrupts.readStatus();
    if (statusBefore != 0 && m_customExitHandler != 0 && !m_inCustomExitHandler)
    {
        invokeCustomExitHandler();
    }

    m_dispatcher.serviceInterrupts(m_interrupts, m_events, m_criticalSectionDepth, &m_logger);
}

u32 PsxSystem::resolveCustomExitCallback(u32 address) const
{
    const Address physical = normalizeAddress(address);
    if (physical > MemoryMap::RAM_SIZE - sizeof(u32))
    {
        return address;
    }

    const u32 tableTarget = readFromRegion<u32>(m_ram.data(), physical, MemoryMap::RAM_SIZE);
    const Address tableTargetPhysical = normalizeAddress(tableTarget);
    if ((tableTarget & 0xE0000000u) == 0x80000000u &&
        tableTargetPhysical <= (MemoryMap::RAM_SIZE - sizeof(u32)) &&
        (tableTargetPhysical & 0x3u) == 0u)
    {
        return tableTarget;
    }
    return address;
}

void PsxSystem::invokeCustomExitHandler()
{
    m_inCustomExitHandler = true;
    invokeCallback(resolveCustomExitCallback(m_customExitHandler));
    m_inCustomExitHandler = false;
}

void PsxSystem::invokeCallback(u32 address)
{
    if (address == 0)
    {
        return;
    }
    if (m_callbackInvoker)
    {
        const bool previousInCallbackInvocation = m_inCallbackInvocation;
        m_inCallbackInvocation = true;
        try
        {
            m_callbackInvoker(address);
        }
        catch (const ReturnFromExceptionSignal&)
        {
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
