#include "psxrecomp/runtime/psx_system.h"

namespace psxrecomp
{
namespace runtime
{

namespace
{

u16 readSpuHalfword(Spu& spu, Address offset)
{
    if (offset >= Mmio::SPU_SIZE)
    {
        return 0;
    }
    return spu.readRegister(static_cast<u32>(offset));
}

u32 readSpuWord(Spu& spu, Address offset)
{
    const u32 low = static_cast<u32>(readSpuHalfword(spu, offset));
    const u32 high = static_cast<u32>(readSpuHalfword(spu, offset + 2u));
    return low | (high << 16);
}

void writeSpuHalfword(Spu& spu, Address offset, u16 value)
{
    if ((offset & 1u) != 0 || offset >= Mmio::SPU_SIZE)
    {
        return;
    }
    spu.writeRegister(static_cast<u32>(offset), value);
}

void writeSpuWord(Spu& spu, Address offset, u32 value)
{
    writeSpuHalfword(spu, offset, static_cast<u16>(value & 0xFFFFu));
    writeSpuHalfword(spu, offset + 2u, static_cast<u16>(value >> 16));
}

void recordMmioReadWatch(DiagWatchpointEngine& watchpoints, RuntimeDebugOverlay& overlay,
                         RuntimeLogger& logger, Address address, u8 size, u32 value,
                         Address resumeAddress)
{
    if (!watchpoints.shouldWatchMmioRead(address, size))
    {
        return;
    }
    watchpoints.recordMmioRead(overlay.lastProgramCounter(), address, size, value, &logger,
                               resumeAddress);
}

void recordMmioWriteWatch(DiagWatchpointEngine& watchpoints, RuntimeDebugOverlay& overlay,
                          RuntimeLogger& logger, Address address, u8 size, u32 value,
                          Address resumeAddress)
{
    if (!watchpoints.shouldWatchMmioWrite(address, size))
    {
        return;
    }
    watchpoints.recordMmioWrite(overlay.lastProgramCounter(), address, size, value, &logger,
                                resumeAddress);
}

} // namespace

u32 PsxSystem::readMmio32(Address address)
{
    if (address == Mmio::GPU_GP1)
    {
        const u32 val = m_gpu.pollStatus();
        m_stallClassifier.recordMmioAccess(address, val, false);
        m_diagTracepoints.recordMmioRead(address, val, m_debugOverlay.lastProgramCounter(),
                                         &m_logger);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                            m_lastResumeAddress);
        return val;
    }
    if (address == Mmio::GPU_GP0)
    {
        const u32 val = m_gpu.readData();
        m_stallClassifier.recordMmioAccess(address, val, false);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                            m_lastResumeAddress);
        return val;
    }
    if (address == Mmio::MDEC_BASE)
    {
        const u32 val = m_mdec.readData();
        m_stallClassifier.recordMmioAccess(address, val, false);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                            m_lastResumeAddress);
        return val;
    }
    if (address == Mmio::MDEC_BASE + 4)
    {
        const u32 val = m_mdec.readStatus();
        m_stallClassifier.recordMmioAccess(address, val, false);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                            m_lastResumeAddress);
        return val;
    }
    if (address == Mmio::INTERRUPT_STATUS)
    {
        const u32 val = m_interrupts.readStatus();
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                            m_lastResumeAddress);
        return val;
    }
    if (address == Mmio::INTERRUPT_MASK)
    {
        const u32 val = m_interrupts.readMask();
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                            m_lastResumeAddress);
        return val;
    }
    if (isInRange(address, Mmio::DMA_BASE, Mmio::DMA_SIZE))
    {
        const u32 val = m_dma.readRegister(address);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                            m_lastResumeAddress);
        return val;
    }
    if (isInRange(address, Mmio::CONTROLLER_BASE, Mmio::CONTROLLER_SIZE))
    {
        const u32 val = m_sio0.read32(address - Mmio::CONTROLLER_BASE);
        m_stallClassifier.recordMmioAccess(address, val, false);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                            m_lastResumeAddress);
        return val;
    }
    if (isInRange(address, Mmio::SPU_BASE, Mmio::SPU_SIZE))
    {
        const u32 val = readSpuWord(m_spu, address - Mmio::SPU_BASE);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                            m_lastResumeAddress);
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
        {
            const u32 val = static_cast<u32>(m_timers.readCounter(timerIndex));
            recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                                m_lastResumeAddress);
            return val;
        }
        case 0x4:
        {
            const u32 val = static_cast<u32>(m_timers.readMode(timerIndex));
            recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                                m_lastResumeAddress);
            return val;
        }
        case 0x8:
        {
            const u32 val = static_cast<u32>(m_timers.readTarget(timerIndex));
            recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, val,
                                m_lastResumeAddress);
            return val;
        }
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
        const u16 val = static_cast<u16>(m_interrupts.readStatus() & 0xFFFFu);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 2, val,
                            m_lastResumeAddress);
        return val;
    }
    if (address == Mmio::INTERRUPT_MASK)
    {
        const u16 val = static_cast<u16>(m_interrupts.readMask() & 0xFFFFu);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 2, val,
                            m_lastResumeAddress);
        return val;
    }
    if (isInRange(address, Mmio::SPU_BASE, Mmio::SPU_SIZE))
    {
        const u16 val = m_spu.readRegister(address - Mmio::SPU_BASE);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 2, val,
                            m_lastResumeAddress);
        return val;
    }
    if (isInRange(address, Mmio::CONTROLLER_BASE, Mmio::CONTROLLER_SIZE))
    {
        const u16 val = m_sio0.read16(address - Mmio::CONTROLLER_BASE);
        m_stallClassifier.recordMmioAccess(address, val, false);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 2, val,
                            m_lastResumeAddress);
        return val;
    }
    if (isInRange(address, Mmio::TIMER_BASE, Mmio::TIMER_SIZE))
    {
        const Address offset = address - Mmio::TIMER_BASE;
        const size_t timerIndex = static_cast<size_t>(offset / 0x10);
        switch (offset & 0xF)
        {
        case 0x0:
        {
            const u16 val = m_timers.readCounter(timerIndex);
            recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 2, val,
                                m_lastResumeAddress);
            return val;
        }
        case 0x4:
        {
            const u16 val = m_timers.readMode(timerIndex);
            recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 2, val,
                                m_lastResumeAddress);
            return val;
        }
        case 0x8:
        {
            const u16 val = m_timers.readTarget(timerIndex);
            recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 2, val,
                                m_lastResumeAddress);
            return val;
        }
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
        const u8 offset = static_cast<u8>(address - Mmio::CDROM_BASE);
        const u8 bank = m_cdrom.readStatus() & 0x3u;
        const u8 val = m_cdrom.readReg(offset);
        m_stallClassifier.recordMmioAccess(address, val, false);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 1, val,
                            m_lastResumeAddress);
        if (m_diagCdromBankTracer.isEnabled())
        {
            m_diagCdromBankTracer.recordRead(offset, bank, val);
        }
        return val;
    }
    if (isInRange(address, Mmio::CONTROLLER_BASE, Mmio::CONTROLLER_SIZE))
    {
        const u8 val = m_sio0.read8(address - Mmio::CONTROLLER_BASE);
        m_stallClassifier.recordMmioAccess(address, val, false);
        recordMmioReadWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 1, val,
                            m_lastResumeAddress);
        return val;
    }

    return 0;
}

void PsxSystem::writeMmio32(Address address, u32 value)
{
    recordMmioWriteWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 4, value,
                         m_lastResumeAddress);
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
        const u32 before = m_interrupts.readStatus();
        m_interrupts.writeStatus(value);
        const u32 after = m_interrupts.readStatus();
        m_cpTimeline.push(CpEventKind::IStatClear, 0, value, before, after);
        syncLevelInterruptSources();
        return;
    }
    if (address == Mmio::INTERRUPT_MASK)
    {
        const u32 before = m_interrupts.readMask();
        m_interrupts.writeMask(value);
        const u32 after = m_interrupts.readMask();
        m_cpTimeline.push(CpEventKind::IMaskWrite, 0, value, before, after);
        syncCop0InterruptPending();
        return;
    }
    if (isInRange(address, Mmio::DMA_BASE, Mmio::DMA_SIZE))
    {
        // Record control-plane timeline events for key DMA register writes.
        if (address == DmaController::InterruptReg)
        {
            const u32 before = m_dma.readRegister(address);
            auto triggered = m_dma.writeRegister(address, value);
            const u32 after = m_dma.readRegister(address);
            m_cpTimeline.push(CpEventKind::DicrWrite, 0, value, before, after);
            if (triggered)
            {
                handleDmaTransfer(*triggered);
            }
        }
        else if (address == DmaController::ControlReg)
        {
            const u32 before = m_dma.readRegister(address);
            auto triggered = m_dma.writeRegister(address, value);
            const u32 after = m_dma.readRegister(address);
            m_cpTimeline.push(CpEventKind::DpcrWrite, 0, value, before, after);
            if (triggered)
            {
                handleDmaTransfer(*triggered);
            }
        }
        else
        {
            // Channel register write: check if it's a CHCR (offset 0x8 in channel stride).
            const bool isChcr =
                (address >= DmaController::ChannelBase) &&
                ((address - DmaController::ChannelBase) % DmaController::ChannelStride) == 8u;
            const u32 before = isChcr ? m_dma.readRegister(address) : 0u;
            auto triggered = m_dma.writeRegister(address, value);
            if (isChcr)
            {
                const u32 after = m_dma.readRegister(address);
                const u8 portIdx = static_cast<u8>(
                    (address - DmaController::ChannelBase) / DmaController::ChannelStride);
                m_cpTimeline.push(CpEventKind::ChcrWrite, portIdx, value, before, after);
            }
            if (triggered)
            {
                handleDmaTransfer(*triggered);
            }
        }
        syncLevelInterruptSources();
        return;
    }
    if (isInRange(address, Mmio::SPU_BASE, Mmio::SPU_SIZE))
    {
        writeSpuWord(m_spu, address - Mmio::SPU_BASE, value);
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
    recordMmioWriteWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 2, value,
                         m_lastResumeAddress);
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
        writeSpuHalfword(m_spu, address - Mmio::SPU_BASE, value);
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
    recordMmioWriteWatch(m_diagWatchpoints, m_debugOverlay, m_logger, address, 1, value,
                         m_lastResumeAddress);
    if (isInRange(address, Mmio::SPU_BASE, Mmio::SPU_SIZE))
    {
        const Address offset = address - Mmio::SPU_BASE;
        if ((offset & 1u) == 0)
        {
            writeSpuHalfword(m_spu, offset, static_cast<u16>(value));
        }
        return;
    }
    if (isInRange(address, Mmio::CDROM_BASE, Mmio::CDROM_SIZE))
    {
        const u8 offset = static_cast<u8>(address - Mmio::CDROM_BASE);
        // Read bank BEFORE the write so offset-0 writes (bank select) are
        // labelled with the OLD bank (the one that was active when the write
        // was dispatched).  Bank changes take effect inside writeReg().
        const u8 bank = m_cdrom.readStatus() & 0x3u;
        m_stallClassifier.recordMmioAccess(address, value, true);
        m_cdrom.writeReg(offset, value);
        if (m_diagCdromBankTracer.isEnabled())
        {
            m_diagCdromBankTracer.recordWrite(offset, bank, value);
        }
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
