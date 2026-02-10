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
constexpr u32 DMA_DIRECTION_FROM_RAM = 0x00000001;

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

    m_logger.log(LogLevel::Info, "system", "Runtime reset complete");
}

void PsxSystem::boot()
{
    m_logger.log(LogLevel::Info, "system", "Runtime boot sequence initialized");
}

void PsxSystem::runFrame()
{
    m_spu.tick(CYCLES_PER_FRAME);
    m_scheduler.tick(CYCLES_PER_FRAME);
    m_debugOverlay.setLastFrameCycles(CYCLES_PER_FRAME);
    m_logger.log(LogLevel::Debug, "perf", m_debugOverlay.renderText());
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
    m_gpu.writeStatus(gpuStatus);

    m_cdrom.reset();
    m_input.reset();
    m_dma.reset();
    m_scheduler.reset();
    m_debugOverlay.reset();
    return true;
}
uint64_t PsxSystem::stateChecksum() const
{
    return fnv1a64(serializeState());
}
void PsxSystem::callBiosSyscall(u32 code, const u32* regs, size_t regCount)
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
        stream << "BIOS EnterCriticalSection";
        m_logger.log(LogLevel::Debug, "bios", stream.str());
        return;
    case 0x01:
        stream << "BIOS ExitCriticalSection";
        m_logger.log(LogLevel::Debug, "bios", stream.str());
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
u32 PsxSystem::readMmio32(Address address)
{
    if (address == Mmio::GPU_GP1)
    {
        return m_gpu.readStatus();
    }
    if (address == Mmio::GPU_GP0)
    {
        return m_gpu.readData();
    }
    if (address == Mmio::INTERRUPT_STATUS)
    {
        return m_interrupts.readStatus();
    }
    if (address == Mmio::INTERRUPT_MASK)
    {
        return m_interrupts.readMask();
    }
    if (isInRange(address, Mmio::DMA_BASE, Mmio::DMA_SIZE))
    {
        return m_dma.readRegister(address);
    }

    return 0;
}

u16 PsxSystem::readMmio16(Address address)
{
    if (isInRange(address, Mmio::SPU_BASE, Mmio::SPU_SIZE))
    {
        return m_spu.readRegister(address - Mmio::SPU_BASE);
    }
    if (isInRange(address, Mmio::CONTROLLER_BASE, Mmio::CONTROLLER_SIZE))
    {
        return m_input.readState();
    }

    return 0;
}

u8 PsxSystem::readMmio8(Address address)
{
    if (isInRange(address, Mmio::CDROM_BASE, Mmio::CDROM_SIZE))
    {
        switch (address - Mmio::CDROM_BASE)
        {
        case 0:
            return m_cdrom.readStatus();
        case 1:
            return m_cdrom.readData();
        case 2:
            return m_cdrom.readInterruptFlags();
        case 3:
            return m_cdrom.readInterruptEnable();
        default:
            return 0;
        }
    }

    return 0;
}

void PsxSystem::writeMmio32(Address address, u32 value)
{
    if (address == Mmio::GPU_GP0)
    {
        m_gpu.writeCommand(value);
        return;
    }
    if (address == Mmio::GPU_GP1)
    {
        m_gpu.writeStatus(value);
        return;
    }
    if (address == Mmio::INTERRUPT_STATUS)
    {
        m_interrupts.writeStatus(value);
        return;
    }
    if (address == Mmio::INTERRUPT_MASK)
    {
        m_interrupts.writeMask(value);
        return;
    }
    if (isInRange(address, Mmio::DMA_BASE, Mmio::DMA_SIZE))
    {
        auto triggered = m_dma.writeRegister(address, value);
        if (triggered)
        {
            handleDmaTransfer(*triggered);
        }
        return;
    }
}

void PsxSystem::writeMmio16(Address address, u16 value)
{
    if (isInRange(address, Mmio::SPU_BASE, Mmio::SPU_SIZE))
    {
        m_spu.writeRegister(address - Mmio::SPU_BASE, value);
        return;
    }
    if (isInRange(address, Mmio::CONTROLLER_BASE, Mmio::CONTROLLER_SIZE))
    {
        (void)value;
        return;
    }
}

void PsxSystem::writeMmio8(Address address, u8 value)
{
    if (isInRange(address, Mmio::CDROM_BASE, Mmio::CDROM_SIZE))
    {
        switch (address - Mmio::CDROM_BASE)
        {
        case 0:
            m_cdrom.writeCommand(value);
            break;
        case 1:
            m_cdrom.writeParam(value);
            break;
        case 2:
            m_cdrom.writeInterruptFlags(value);
            break;
        case 3:
            m_cdrom.writeInterruptEnable(value);
            break;
        default:
            break;
        }
    }
}
void PsxSystem::handleDmaTransfer(DmaPort port)
{
    const auto& channel = m_dma.channel(port);
    u32 wordCount = channel.blockControl & 0xFFFF;
    if (wordCount == 0)
    {
        m_dma.clearTrigger(port);
        return;
    }

    bool fromRam = (channel.channelControl & DMA_DIRECTION_FROM_RAM) != 0;
    if (!fromRam)
    {
        m_dma.clearTrigger(port);
        return;
    }

    Address base = channel.baseAddress & 0x1FFFFC;
    for (u32 i = 0; i < wordCount; ++i)
    {
        u32 value = read<u32>(base + i * sizeof(u32));
        switch (port)
        {
        case DmaPort::Gpu:
            m_gpu.writeDma(value);
            break;
        case DmaPort::Spu:
            m_spu.writeDma(value);
            break;
        case DmaPort::Cdrom:
            m_cdrom.writeDma(value);
            break;
        default:
            break;
        }
    }

    m_dma.clearTrigger(port);
    m_interrupts.raise(InterruptLine::Dma);
    m_debugOverlay.incrementDmaTransfers();
    m_debugOverlay.incrementInterruptsRaised();

    std::ostringstream message;
    message << "DMA transfer on port " << static_cast<int>(port) << " words=" << wordCount;
    m_logger.log(LogLevel::Debug, "dma", message.str());
}

} // namespace runtime
} // namespace psxrecomp
