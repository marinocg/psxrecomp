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
constexpr u32 DMA_DIRECTION_FROM_RAM = 0x00000001;
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

    m_logger.log(LogLevel::Info, "Runtime reset complete");
}

void PsxSystem::boot()
{
    m_logger.log(LogLevel::Info, "Runtime boot sequence initialized");
}

void PsxSystem::runFrame()
{
    m_spu.tick(CYCLES_PER_FRAME);
    m_scheduler.tick(CYCLES_PER_FRAME);
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

void PsxSystem::setDiscSwapInfo(DiscSwapInfo info)
{
    m_discSwapInfo = std::move(info);
}

void PsxSystem::callBiosSyscall(u32 code, const u32* regs, size_t regCount)
{
    std::ostringstream stream;
    stream << "BIOS syscall stub invoked: code=0x" << std::hex << code << std::dec
           << ", regs=" << regCount;
    if (regs != nullptr && regCount > 0)
    {
        stream << ", a0=0x" << std::hex << regs[4];
    }
    m_logger.log(LogLevel::Warn, stream.str());
}

const PsxSystem::DiscSwapInfo& PsxSystem::discSwapInfo() const
{
    return m_discSwapInfo;
}

u32 PsxSystem::readMmio32(Address address)
{
    if (address == Mmio::GPU_GP1)
    {
        return m_gpu.readStatus();
    }
    if (address == Mmio::GPU_GP0)
    {
        return 0;
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
        return m_cdrom.readStatus();
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
        if (address == Mmio::CDROM_BASE)
        {
            m_cdrom.writeCommand(value);
        }
        else
        {
            m_cdrom.writeParam(value);
        }
        return;
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

    std::ostringstream message;
    message << "DMA transfer on port " << static_cast<int>(port) << " words=" << wordCount;
    m_logger.log(LogLevel::Debug, message.str());
}

} // namespace runtime
} // namespace psxrecomp
