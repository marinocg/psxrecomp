#include "psxrecomp/runtime/psx_system.h"

namespace psxrecomp
{
namespace runtime
{

u32 PsxSystem::readMmio32(Address address)
{
    if (address == Mmio::GPU_GP1)
    {
        const u32 val = m_gpu.pollStatus();
        m_stallClassifier.recordMmioAccess(address, val, false);
        return val;
    }
    if (address == Mmio::GPU_GP0)
    {
        const u32 val = m_gpu.readData();
        m_stallClassifier.recordMmioAccess(address, val, false);
        return val;
    }
    if (address == Mmio::MDEC_BASE)
    {
        const u32 val = m_mdec.readData();
        m_stallClassifier.recordMmioAccess(address, val, false);
        return val;
    }
    if (address == Mmio::MDEC_BASE + 4)
    {
        const u32 val = m_mdec.readStatus();
        m_stallClassifier.recordMmioAccess(address, val, false);
        return val;
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
    if (isInRange(address, Mmio::CONTROLLER_BASE, Mmio::CONTROLLER_SIZE))
    {
        const u32 val = m_sio0.read32(address - Mmio::CONTROLLER_BASE);
        m_stallClassifier.recordMmioAccess(address, val, false);
        return val;
    }
    // Timer registers: PSn00bSDK reads Timer1 (HBlank counter used for VSync)
    // via 32-bit LW instructions.  Forward to the 16-bit timer handler.
    if (isInRange(address, Mmio::TIMER_BASE, Mmio::TIMER_SIZE))
    {
        const Address offset = address - Mmio::TIMER_BASE;
        const size_t timerIndex = static_cast<size_t>(offset / 0x10);
        switch (offset & 0xF)
        {
        case 0x0:
            return static_cast<u32>(m_timers.readCounter(timerIndex));
        case 0x4:
            return static_cast<u32>(m_timers.readMode(timerIndex));
        case 0x8:
            return static_cast<u32>(m_timers.readTarget(timerIndex));
        default:
            return 0;
        }
    }

    return 0;
}

u16 PsxSystem::readMmio16(Address address)
{
    if (address == Mmio::INTERRUPT_STATUS)
    {
        return static_cast<u16>(m_interrupts.readStatus() & 0xFFFFu);
    }
    if (address == Mmio::INTERRUPT_MASK)
    {
        return static_cast<u16>(m_interrupts.readMask() & 0xFFFFu);
    }
    if (isInRange(address, Mmio::SPU_BASE, Mmio::SPU_SIZE))
    {
        return m_spu.readRegister(address - Mmio::SPU_BASE);
    }
    if (isInRange(address, Mmio::CONTROLLER_BASE, Mmio::CONTROLLER_SIZE))
    {
        const u16 val = m_sio0.read16(address - Mmio::CONTROLLER_BASE);
        m_stallClassifier.recordMmioAccess(address, val, false);
        return val;
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
        const u8 val = m_cdrom.readReg(static_cast<u8>(address - Mmio::CDROM_BASE));
        m_stallClassifier.recordMmioAccess(address, val, false);
        return val;
    }
    if (isInRange(address, Mmio::CONTROLLER_BASE, Mmio::CONTROLLER_SIZE))
    {
        const u8 val = m_sio0.read8(address - Mmio::CONTROLLER_BASE);
        m_stallClassifier.recordMmioAccess(address, val, false);
        return val;
    }

    return 0;
}

void PsxSystem::writeMmio32(Address address, u32 value)
{
    if (address == Mmio::GPU_GP0)
    {
        m_gpu.writeCommand(value);
        syncLevelInterruptSources();
        return;
    }
    if (address == Mmio::GPU_GP1)
    {
        m_gpu.writeStatus(value);
        syncLevelInterruptSources();
        return;
    }
    if (address == Mmio::MDEC_BASE)
    {
        m_stallClassifier.recordMmioAccess(address, value, true);
        m_mdec.writeCommand(value);
        return;
    }
    if (address == Mmio::MDEC_BASE + 4)
    {
        m_stallClassifier.recordMmioAccess(address, value, true);
        m_mdec.writeControl(value);
        return;
    }
    if (address == Mmio::INTERRUPT_STATUS)
    {
        m_interrupts.writeStatus(value);
        syncLevelInterruptSources();
        return;
    }
    if (address == Mmio::INTERRUPT_MASK)
    {
        m_interrupts.writeMask(value);
        syncCop0InterruptPending();
        return;
    }
    if (isInRange(address, Mmio::DMA_BASE, Mmio::DMA_SIZE))
    {
        auto triggered = m_dma.writeRegister(address, value);
        if (triggered)
        {
            handleDmaTransfer(*triggered);
        }
        syncLevelInterruptSources();
        return;
    }
    if (isInRange(address, Mmio::CONTROLLER_BASE, Mmio::CONTROLLER_SIZE))
    {
        m_sio0.write32(address - Mmio::CONTROLLER_BASE, value);
        return;
    }
    // Timer registers: some code writes timers with 32-bit SW instructions.
    if (isInRange(address, Mmio::TIMER_BASE, Mmio::TIMER_SIZE))
    {
        const Address offset = address - Mmio::TIMER_BASE;
        const size_t timerIndex = static_cast<size_t>(offset / 0x10);
        switch (offset & 0xF)
        {
        case 0x0:
            m_timers.writeCounter(timerIndex, static_cast<u16>(value));
            break;
        case 0x4:
            m_timers.writeMode(timerIndex, static_cast<u16>(value));
            break;
        case 0x8:
            m_timers.writeTarget(timerIndex, static_cast<u16>(value));
            break;
        default:
            break;
        }
        return;
    }
}

void PsxSystem::writeMmio16(Address address, u16 value)
{
    if (address == Mmio::INTERRUPT_STATUS)
    {
        const u32 mergedStatus =
            (m_interrupts.readStatus() & 0xFFFF0000u) | static_cast<u32>(value);
        m_interrupts.writeStatus(mergedStatus);
        syncLevelInterruptSources();
        return;
    }
    if (address == Mmio::INTERRUPT_MASK)
    {
        const u32 mergedMask = (m_interrupts.readMask() & 0xFFFF0000u) | static_cast<u32>(value);
        m_interrupts.writeMask(mergedMask);
        syncCop0InterruptPending();
        return;
    }
    if (isInRange(address, Mmio::SPU_BASE, Mmio::SPU_SIZE))
    {
        m_spu.writeRegister(address - Mmio::SPU_BASE, value);
        return;
    }
    if (isInRange(address, Mmio::CONTROLLER_BASE, Mmio::CONTROLLER_SIZE))
    {
        m_sio0.write16(address - Mmio::CONTROLLER_BASE, value);
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
        m_stallClassifier.recordMmioAccess(address, value, true);
        m_cdrom.writeReg(static_cast<u8>(address - Mmio::CDROM_BASE), value);
        syncLevelInterruptSources();
        return;
    }
    if (isInRange(address, Mmio::CONTROLLER_BASE, Mmio::CONTROLLER_SIZE))
    {
        m_stallClassifier.recordMmioAccess(address, value, true);
        m_sio0.write8(address - Mmio::CONTROLLER_BASE, value);
    }
}

} // namespace runtime
} // namespace psxrecomp
