#include "psxrecomp/runtime/psx_system.h"

namespace psxrecomp
{
namespace runtime
{

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
    if (isInRange(address, Mmio::TIMER_BASE, Mmio::TIMER_SIZE))
    {
        const Address offset = address - Mmio::TIMER_BASE;
        const size_t timerIndex = static_cast<size_t>(offset / 0x10);
        switch (offset & 0xF)
        {
        case 0x0:
            return m_timers.readCounter(timerIndex);
        case 0x4:
            return m_timers.readMode(timerIndex);
        case 0x8:
            return m_timers.readTarget(timerIndex);
        default:
            return 0;
        }
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
    if (isInRange(address, Mmio::TIMER_BASE, Mmio::TIMER_SIZE))
    {
        const Address offset = address - Mmio::TIMER_BASE;
        const size_t timerIndex = static_cast<size_t>(offset / 0x10);
        switch (offset & 0xF)
        {
        case 0x0:
            m_timers.writeCounter(timerIndex, value);
            break;
        case 0x4:
            m_timers.writeMode(timerIndex, value);
            break;
        case 0x8:
            m_timers.writeTarget(timerIndex, value);
            break;
        default:
            break;
        }
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
            if (m_cdrom.hasIrqRequest() &&
                (m_interrupts.readStatus() & static_cast<u32>(InterruptLine::Cdrom)) == 0)
            {
                m_interrupts.raise(InterruptLine::Cdrom);
                m_debugOverlay.incrementInterruptsRaised();
            }
            break;
        case 1:
            m_cdrom.writeParam(value);
            break;
        case 2:
            m_cdrom.writeInterruptFlags(value);
            break;
        case 3:
            m_cdrom.writeInterruptEnable(value);
            if (m_cdrom.hasIrqRequest() &&
                (m_interrupts.readStatus() & static_cast<u32>(InterruptLine::Cdrom)) == 0)
            {
                m_interrupts.raise(InterruptLine::Cdrom);
                m_debugOverlay.incrementInterruptsRaised();
            }
            break;
        default:
            break;
        }
    }
}

} // namespace runtime
} // namespace psxrecomp
