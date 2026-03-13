#include "psxrecomp/runtime/interrupt_controller.h"
#include "psxrecomp/runtime/psx_system.h"

#include <stdexcept>

namespace
{
using psxrecomp::Address;
using psxrecomp::u16;
using psxrecomp::u32;
using psxrecomp::u8;
using psxrecomp::runtime::DmaController;
using psxrecomp::runtime::DmaPort;
using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::PsxSystem;
using psxrecomp::runtime::Spu;

constexpr u32 kHandshakeDelayCycles = 0x300u;
constexpr u32 kCyclesPerSample = 768u;
constexpr u32 kStatusIrqFlag = 1u << 6;
constexpr u32 kSpuControlEnableIrq = 0xC040u;
constexpr u32 kSpuControlManualWriteIrq = 0xC050u;
constexpr u32 kSpuControlDmaWriteIrq = 0xC060u;
constexpr u32 kSpuControlStopNoIrq = 0xC000u;

Address spuRegisterAddress(u32 rawOffset)
{
    return psxrecomp::runtime::Mmio::SPU_BASE + (rawOffset & 0x1FFu);
}

constexpr u32 packBytes(u8 b0, u8 b1, u8 b2, u8 b3)
{
    return static_cast<u32>(b0) | (static_cast<u32>(b1) << 8) | (static_cast<u32>(b2) << 16) |
           (static_cast<u32>(b3) << 24);
}

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

} // namespace

int main()
{
    PsxSystem system;
    require(system.initialize(), "failed to initialize SPU IRQ test system");

    const Address irqAddrReg = spuRegisterAddress(Spu::RegisterMap::IrqAddress);
    const Address transferAddrReg = spuRegisterAddress(Spu::RegisterMap::RamTransferAddress);
    const Address transferFifoReg = spuRegisterAddress(Spu::RegisterMap::RamTransferData);
    const Address transferCtrlReg = spuRegisterAddress(Spu::RegisterMap::TransferControl);
    const Address controlReg = spuRegisterAddress(Spu::RegisterMap::Control);
    const Address statusReg = spuRegisterAddress(Spu::RegisterMap::Status);
    const Address spuDmaBase = DmaController::ChannelBase +
                               DmaController::ChannelStride * static_cast<Address>(DmaPort::Spu);
    const u32 spuLine = static_cast<u32>(InterruptLine::Spu);

    system.writeMmioExplicit<u16>(transferCtrlReg, 0x0004u);
    system.writeMmioExplicit<u16>(irqAddrReg, 0x0000u);
    require(system.readMmioExplicit<u16>(irqAddrReg) == 0x0000u,
            "SPU IRQ address did not read back");

    // Manual transfer hitting DA4 should latch SPUSTAT.6 and assert I_STAT SPU.
    system.writeMmioExplicit<u16>(controlReg, kSpuControlManualWriteIrq);
    system.tickCpuCycles(kHandshakeDelayCycles);
    system.writeMmioExplicit<u16>(transferAddrReg, 0x0000u);
    system.writeMmioExplicit<u16>(transferFifoReg, 0x1122u);
    system.writeMmioExplicit<u16>(transferFifoReg, 0x3344u);
    system.writeMmioExplicit<u16>(controlReg, kSpuControlManualWriteIrq);
    system.tickCpuCycles(kHandshakeDelayCycles);
    require((system.readMmioExplicit<u16>(statusReg) & kStatusIrqFlag) != 0u,
            "manual transfer did not latch SPUSTAT IRQ flag");
    require((system.interrupts().readStatus() & spuLine) != 0u,
            "manual transfer did not assert SPU interrupt line");

    // Clearing I_STAT alone should not suppress a still-latched SPU IRQ.
    system.interrupts().writeStatus(~spuLine);
    require((system.interrupts().readStatus() & spuLine) == 0u, "failed to clear I_STAT SPU bit");
    system.serviceInterrupts();
    require((system.interrupts().readStatus() & spuLine) != 0u,
            "latched SPU IRQ did not reassert through the system interrupt path");

    // SPUCNT bit6 acknowledge should clear SPUSTAT.6 and stop re-assertion.
    system.writeMmioExplicit<u16>(controlReg, kSpuControlStopNoIrq);
    require((system.readMmioExplicit<u16>(statusReg) & kStatusIrqFlag) == 0u,
            "control-path acknowledge did not clear SPUSTAT IRQ flag");
    system.interrupts().writeStatus(~spuLine);
    system.serviceInterrupts();
    require((system.interrupts().readStatus() & spuLine) == 0u,
            "acknowledged SPU IRQ reasserted unexpectedly");

    // DMA write hitting DA4 should also latch and raise the SPU IRQ.
    system.write<psxrecomp::u32>(0x00014000u, 0xAABBCCDDu);
    system.writeMmioExplicit<u16>(irqAddrReg, 0x0002u);
    system.writeMmioExplicit<u16>(transferAddrReg, 0x0002u);
    system.writeMmioExplicit<u16>(controlReg, kSpuControlDmaWriteIrq);
    system.tickCpuCycles(kHandshakeDelayCycles);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x0, 0x00014000u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x4, 0x00000001u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x8, 0x01000001u);
    system.serviceInterrupts();
    require((system.readMmioExplicit<u16>(statusReg) & kStatusIrqFlag) != 0u,
            "DMA write did not latch SPUSTAT IRQ flag");
    require((system.interrupts().readStatus() & spuLine) != 0u,
            "DMA write did not assert SPU interrupt line");

    system.writeMmioExplicit<u16>(controlReg, kSpuControlStopNoIrq);
    system.interrupts().writeStatus(~spuLine);
    system.serviceInterrupts();
    require((system.interrupts().readStatus() & spuLine) == 0u,
            "DMA-triggered SPU IRQ did not clear cleanly");

    // Voice fetch over DA4 should latch and raise the SPU IRQ.
    system.spu().writeRegister(Spu::RegisterMap::RamTransferAddress, 0x0004u);
    system.spu().writeDma(packBytes(0x00u, 0x00u, 0x11u, 0x11u));
    system.spu().writeDma(packBytes(0x11u, 0x11u, 0x11u, 0x11u));
    system.spu().writeDma(packBytes(0x11u, 0x11u, 0x11u, 0x11u));
    system.spu().writeDma(packBytes(0x11u, 0x11u, 0x11u, 0x11u));

    system.spu().writeRegister(0x000u, 0x3FFFu);
    system.spu().writeRegister(0x002u, 0x3FFFu);
    system.spu().writeRegister(0x004u, 0x1000u);
    system.spu().writeRegister(0x006u, 0x0004u);

    system.writeMmioExplicit<u16>(irqAddrReg, 0x0004u);
    system.writeMmioExplicit<u16>(controlReg, kSpuControlEnableIrq);
    system.interrupts().writeStatus(~spuLine);
    system.tickCpuCycles(kHandshakeDelayCycles);
    system.spu().writeRegister(Spu::RegisterMap::KeyOnLow, 0x0001u);
    system.tickCpuCycles(kCyclesPerSample);
    require((system.readMmioExplicit<u16>(statusReg) & kStatusIrqFlag) != 0u,
            "voice fetch did not latch SPUSTAT IRQ flag");
    require((system.interrupts().readStatus() & spuLine) != 0u,
            "voice fetch did not assert SPU interrupt line");

    return 0;
}
