#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
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

void appendU32(std::vector<u8>& out, u32 value)
{
    out.push_back(static_cast<u8>(value & 0xFF));
    out.push_back(static_cast<u8>((value >> 8) & 0xFF));
    out.push_back(static_cast<u8>((value >> 16) & 0xFF));
    out.push_back(static_cast<u8>((value >> 24) & 0xFF));
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

uint64_t fnv1a64(const std::vector<u8>& bytes)
{
    uint64_t hash = 1469598103934665603ull;
    for (u8 byte : bytes)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
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
    m_scheduler.reset();
    m_debugOverlay.reset();
    m_timers.reset();
    m_criticalSectionDepth = 0;

    m_logger.log(LogLevel::Info, "system", "Runtime reset complete");
}

void PsxSystem::boot()
{
    m_logger.log(LogLevel::Info, "system", "Runtime boot sequence initialized");
}

void PsxSystem::runFrame()
{
    // Advance GPU command consumption so GPUSTAT ready/request bits evolve over time.
    // Drain up to one full FIFO worth of words per frame (64 words, 2 cycles/word).
    // Without this, a saturated FIFO can remain permanently "not ready", causing
    // DrawSync/VSync-style polling loops in game code to spin forever.
    m_gpu.tickGpu(GPU_FIFO_DRAIN_CYCLES_PER_FRAME);
    m_spu.tick(CYCLES_PER_FRAME);
    m_cdrom.tick(CYCLES_PER_FRAME);
    m_timers.tick(CYCLES_PER_FRAME,
                  [this](InterruptLine line)
                  {
                      m_interrupts.raise(line);
                      m_debugOverlay.incrementInterruptsRaised();
                  });
    if (m_cdrom.hasIrqRequest() &&
        (m_interrupts.readStatus() & static_cast<u32>(InterruptLine::Cdrom)) == 0)
    {
        m_interrupts.raise(InterruptLine::Cdrom);
        m_debugOverlay.incrementInterruptsRaised();
    }
    m_scheduler.tick(CYCLES_PER_FRAME);
    m_interrupts.raise(InterruptLine::VBlank);
    m_debugOverlay.incrementInterruptsRaised();
    // Toggle GPU interlace field (bit 31 of GPUSTAT).  PSn00bSDK VSync
    // detects frame boundaries by XOR-ing consecutive GPUSTAT reads and
    // checking if bit 31 changed.
    m_gpu.tickDisplayLine();
    // Simulate VBlank IRQ delivery: increment the vsync counter in RAM
    // that PSn00bSDK's VBlank handler would normally update.  Without
    // this, VSync(0) loops forever waiting for the counter to change.
    if (m_vsyncCounterAddress != 0)
    {
        const Address offset = (m_vsyncCounterAddress & 0x1FFFFF);
        if (offset + 4 <= MemoryMap::RAM_SIZE)
        {
            u32 counter = 0;
            std::memcpy(&counter, m_ram.data() + offset, sizeof(u32));
            ++counter;
            std::memcpy(m_ram.data() + offset, &counter, sizeof(u32));
        }
    }
    m_debugOverlay.setLastFrameCycles(CYCLES_PER_FRAME);
    m_logger.log(LogLevel::Debug, "perf", m_debugOverlay.renderText());
    ++m_frameCount;
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
    m_autoFrameProgressOnInterruptPoll = enabled;
}

void PsxSystem::setVsyncCounterAddress(Address address)
{
    m_vsyncCounterAddress = address;
    m_logger.log(
        LogLevel::Info, "system",
        "VSync counter registered at 0x" +
            [&]()
            {
                std::ostringstream s;
                s << std::hex << address;
                return s.str();
            }());
}

void PsxSystem::setDrawSyncBusyAddress(Address address)
{
    m_drawSyncBusyAddress = address;
    m_logger.log(
        LogLevel::Info, "system",
        "DrawSync busy byte registered at 0x" +
            [&]()
            {
                std::ostringstream s;
                s << std::hex << address;
                return s.str();
            }());
}

void PsxSystem::clearDrawSyncBusy()
{
    if (m_drawSyncBusyAddress != 0)
    {
        const Address offset = m_drawSyncBusyAddress & 0x1FFFFF;
        if (offset < MemoryMap::RAM_SIZE)
        {
            m_ram[offset] = 0;
        }
    }
}

void PsxSystem::onVsyncCounterRead()
{
    // Re-entrancy guard: runFrame() may trigger reads that hit this again.
    m_inVsyncCounterRead = true;

    // Lightweight frame progression for VSync counter polling:
    // Only increment the counter and toggle the GPU display phase.
    // We do NOT call the full runFrame() here because it ticks SPU,
    // CDROM, timers, scheduler etc. and is too expensive to call on
    // every VSync poll iteration.
    if (m_gpu.inActiveDisplay())
    {
        // Transition through VBlank phases so the counter increments
        m_gpu.tickDisplayLine(); // ActiveDisplay → VBlankStart
        m_gpu.tickDisplayLine(); // VBlankStart → VBlankEnd
        m_gpu.tickDisplayLine(); // VBlankEnd → ActiveDisplay

        // Increment the counter in RAM
        if (m_vsyncCounterAddress != 0)
        {
            const Address offset = (m_vsyncCounterAddress & 0x1FFFFF);
            if (offset + 4 <= MemoryMap::RAM_SIZE)
            {
                u32 counter = 0;
                std::memcpy(&counter, m_ram.data() + offset, sizeof(u32));
                ++counter;
                std::memcpy(m_ram.data() + offset, &counter, sizeof(u32));
            }
        }
        ++m_frameCount;
    }
    else
    {
        // Already in VBlank — step through phases
        m_gpuStatReadCount++;
        if (m_gpuStatReadCount >= 2)
        {
            m_gpu.tickDisplayLine();
            m_gpuStatReadCount = 0;
        }
    }

    m_inVsyncCounterRead = false;
}

u32 PsxSystem::frameCount() const
{
    return m_frameCount;
}

u32 PsxSystem::advanceFrame()
{
    runFrame();
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

std::vector<u8> PsxSystem::serializeState() const
{
    std::vector<u8> state;
    state.reserve(sizeof(u32) * 7 + m_ram.size() + m_scratchpad.size() + m_bios.size());

    appendU32(state, static_cast<u32>(m_ram.size()));
    state.insert(state.end(), m_ram.begin(), m_ram.end());

    appendU32(state, static_cast<u32>(m_scratchpad.size()));
    state.insert(state.end(), m_scratchpad.begin(), m_scratchpad.end());

    appendU32(state, static_cast<u32>(m_bios.size()));
    state.insert(state.end(), m_bios.begin(), m_bios.end());

    appendU32(state, m_interrupts.readStatus());
    appendU32(state, m_interrupts.readMask());
    appendU32(state, m_spu.cyclesElapsed());
    appendU32(state, m_gpu.readStatus());
    return state;
}
bool PsxSystem::deserializeState(const std::vector<u8>& state)
{
    size_t cursor = 0;
    u32 ramSize = 0;
    u32 scratchpadSize = 0;
    u32 biosSize = 0;
    u32 irqStatus = 0;
    u32 irqMask = 0;
    u32 spuCycles = 0;
    u32 gpuStatus = 0;

    auto readBlob = [&state, &cursor](u32 blobSize, std::vector<u8>& out)
    {
        if (cursor > state.size() || blobSize > (state.size() - cursor))
        {
            return false;
        }

        out.assign(state.begin() + static_cast<std::ptrdiff_t>(cursor),
                   state.begin() + static_cast<std::ptrdiff_t>(cursor + blobSize));
        cursor += blobSize;
        return true;
    };

    std::vector<u8> ramCopy;
    std::vector<u8> scratchpadCopy;
    std::vector<u8> biosCopy;

    if (!consumeU32(state, cursor, ramSize) || ramSize != m_ram.size() ||
        !readBlob(ramSize, ramCopy) || !consumeU32(state, cursor, scratchpadSize) ||
        scratchpadSize != m_scratchpad.size() || !readBlob(scratchpadSize, scratchpadCopy) ||
        !consumeU32(state, cursor, biosSize) || biosSize != m_bios.size() ||
        !readBlob(biosSize, biosCopy) || !consumeU32(state, cursor, irqStatus) ||
        !consumeU32(state, cursor, irqMask) || !consumeU32(state, cursor, spuCycles) ||
        !consumeU32(state, cursor, gpuStatus) || cursor != state.size())
    {
        return false;
    }

    std::copy(ramCopy.begin(), ramCopy.end(), m_ram.begin());
    std::copy(scratchpadCopy.begin(), scratchpadCopy.end(), m_scratchpad.begin());
    std::copy(biosCopy.begin(), biosCopy.end(), m_bios.begin());

    m_interrupts.restoreState(irqStatus, irqMask);

    m_spu.reset();
    m_spu.tick(spuCycles);

    m_gpu.reset();
    m_gpu.restoreStatus(gpuStatus);

    m_cdrom.reset();
    m_input.reset();
    m_dma.reset();
    m_scheduler.reset();
    m_debugOverlay.reset();
    m_timers.reset();
    return true;
}
uint64_t PsxSystem::stateChecksum() const
{
    return fnv1a64(serializeState());
}
void PsxSystem::callBiosSyscall(u32 code, u32* regs, size_t regCount)
{
    if (regs == nullptr || regCount == 0)
    {
        m_logger.log(LogLevel::Warn, "bios", "BIOS syscall called with empty register file");
        return;
    }

    std::ostringstream stream;
    switch (code)
    {
    case 0x00:
    {
        // On PS1, syscall(0) dispatches kernel critical-section helpers
        // using a0 as a subcommand (1=enter, 2=exit).
        const u32 subcommand = regCount > 4 ? regs[4] : 0;
        if (subcommand == 1)
        {
            regs[2] = m_criticalSectionDepth > 0 ? 1u : 0u;
            ++m_criticalSectionDepth;
            stream << "BIOS EnterCriticalSection depth=" << m_criticalSectionDepth;
            m_logger.log(LogLevel::Debug, "bios", stream.str());
            return;
        }
        if (subcommand == 2)
        {
            regs[2] = m_criticalSectionDepth > 0 ? 1u : 0u;
            if (m_criticalSectionDepth > 0)
            {
                --m_criticalSectionDepth;
            }
            stream << "BIOS ExitCriticalSection depth=" << m_criticalSectionDepth;
            m_logger.log(LogLevel::Debug, "bios", stream.str());
            return;
        }

        stream << "BIOS syscall(0) stub subcommand a0=0x" << std::hex << subcommand;
        m_logger.log(LogLevel::Warn, "bios", stream.str());
        return;
    }
    case 0x01:
        stream << "BIOS syscall(1) stub";
        m_logger.log(LogLevel::Warn, "bios", stream.str());
        return;
    case 0x3F:
        if (regCount <= 4)
        {
            m_logger.log(LogLevel::Warn, "bios", "BIOS Putchar called without a0 register");
            return;
        }
        stream << "BIOS Putchar: '" << static_cast<char>(regs[4] & 0xFF) << "'";
        m_logger.log(LogLevel::Info, "bios", stream.str());
        return;
    default:
        stream << "BIOS syscall stub invoked: code=0x" << std::hex << code << std::dec
               << ", regs=" << regCount;
        if (regCount > 4)
        {
            stream << ", a0=0x" << std::hex << regs[4];
        }
        m_logger.log(LogLevel::Warn, "bios", stream.str());
        return;
    }
}

} // namespace runtime
} // namespace psxrecomp
