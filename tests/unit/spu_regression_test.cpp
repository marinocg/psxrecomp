#include "psxrecomp/runtime/interrupt_controller.h"
#include "psxrecomp/runtime/psx_system.h"
#include "psxrecomp/runtime/spu.h"

#include <sstream>
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

constexpr u16 kStatusIrqFlag = 1u << 6;
constexpr u16 kStatusBusy = 1u << 10;
constexpr u32 kHandshakeDelayCycles = 0x300u;
constexpr u32 kCyclesPerSample = 768u;
constexpr u32 kSpuControlStop = 0x0000u;
constexpr u32 kSpuControlManualWrite = 0x0010u;
constexpr u32 kSpuControlDmaWrite = 0x0020u;
constexpr u32 kSpuControlDmaRead = 0x0030u;
constexpr u32 kSpuControlManualWriteIrq = 0xC050u;
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

[[noreturn]] void failValue(const char* message, u32 value)
{
    std::ostringstream stream;
    stream << message << " value=0x" << std::hex << value;
    throw std::runtime_error(stream.str());
}

void writeAdpcmBlock(Spu& spu, u16 addressUnits, u8 flags, u8 payloadByte)
{
    spu.writeRegister(Spu::RegisterMap::RamTransferAddress, addressUnits);
    spu.writeDma(packBytes(0x00u, flags, payloadByte, payloadByte));
    spu.writeDma(packBytes(payloadByte, payloadByte, payloadByte, payloadByte));
    spu.writeDma(packBytes(payloadByte, payloadByte, payloadByte, payloadByte));
    spu.writeDma(packBytes(payloadByte, payloadByte, payloadByte, payloadByte));
}
} // namespace

int main()
{
    {
        PsxSystem system;
        require(system.initialize(), "failed to initialize SPU regression test system");

        constexpr u32 spuWindowMask = 0x1FFu;
        const Address voice0LeftBase = psxrecomp::runtime::Mmio::SPU_BASE + 0x000u;
        const Address keyOnBase =
            psxrecomp::runtime::Mmio::SPU_BASE + (Spu::RegisterMap::KeyOnLow & spuWindowMask);
        const Address irqAddrReg = spuRegisterAddress(Spu::RegisterMap::IrqAddress);
        const Address transferAddrReg = spuRegisterAddress(Spu::RegisterMap::RamTransferAddress);
        const Address transferFifoReg = spuRegisterAddress(Spu::RegisterMap::RamTransferData);
        const Address transferCtrlReg = spuRegisterAddress(Spu::RegisterMap::TransferControl);
        const Address controlReg = spuRegisterAddress(Spu::RegisterMap::Control);
        const Address statusReg = spuRegisterAddress(Spu::RegisterMap::Status);
        const Address spuDmaBase =
            DmaController::ChannelBase +
            DmaController::ChannelStride * static_cast<Address>(DmaPort::Spu);

        // 32-bit SPU MMIO and odd/even 8-bit behavior.
        system.writeMmioExplicit<u16>(voice0LeftBase, 0x2468u);
        require(system.spu().voices()[0].leftVolume == 0x2468u, "16-bit SPU write did not latch");
        system.writeMmioExplicit<u8>(voice0LeftBase + 1u, 0x7Fu);
        require(system.spu().voices()[0].leftVolume == 0x2468u,
                "odd-byte SPU write was not ignored");
        system.writeMmioExplicit<u8>(voice0LeftBase, 0x55u);
        require(system.spu().voices()[0].leftVolume == 0x0055u,
                "even-byte SPU write did not behave like a 16-bit write");
        system.writeMmioExplicit<u32>(keyOnBase, 0x00010001u);
        require(system.spu().voices()[0].isActive && system.spu().voices()[16].isActive,
                "32-bit SPU key-on write did not update paired registers");
        require(system.readMmioExplicit<u32>(keyOnBase) == 0x00010001u,
                "32-bit SPU key-on readback mismatched");

        // Clear boot state set by primeBootState() so delayed-SPUSTAT tests
        // start from a known-zero baseline.
        system.writeMmioExplicit<u16>(controlReg, kSpuControlStop);
        system.tickCpuCycles(kHandshakeDelayCycles);

        // Delayed SPUCNT -> SPUSTAT.
        system.writeMmioExplicit<u16>(controlReg, kSpuControlManualWrite);
        require((system.readMmioExplicit<u16>(statusReg) & 0x003Fu) == 0u,
                "SPUSTAT mode bits applied immediately");
        require((system.readMmioExplicit<u16>(statusReg) & kStatusBusy) != 0u,
                "SPUSTAT busy did not assert during control-delay window");
        system.tickCpuCycles(kHandshakeDelayCycles - 1u);
        require((system.readMmioExplicit<u16>(statusReg) & 0x003Fu) == 0u,
                "SPUSTAT mode bits applied too early");
        system.tickCpuCycles(1u);
        require((system.readMmioExplicit<u16>(statusReg) & 0x003Fu) == kSpuControlManualWrite,
                "SPUSTAT mode bits did not appear after the control delay");

        // Visible transfer address vs internal current address.
        system.writeMmioExplicit<u16>(transferCtrlReg, 0x0004u);
        system.writeMmioExplicit<u16>(controlReg, kSpuControlStop);
        system.tickCpuCycles(kHandshakeDelayCycles);
        system.writeMmioExplicit<u16>(transferAddrReg, 0x0001u);
        system.writeMmioExplicit<u16>(transferFifoReg, 0x1122u);
        system.writeMmioExplicit<u16>(transferFifoReg, 0x3344u);
        system.writeMmioExplicit<u16>(controlReg, kSpuControlManualWrite);
        system.tickCpuCycles(kHandshakeDelayCycles);
        system.tickCpuCycles(kHandshakeDelayCycles * 2u);
        require(system.spu().ramWords()[2] == 0x33441122u,
                "manual transfer did not use the modeled internal current address");
        require(system.readMmioExplicit<u16>(transferAddrReg) == 0x0001u,
                "visible transfer address changed during manual transfer");

        // DMA read/write gating.
        system.write<psxrecomp::u32>(0x00014000u, 0xAABBCCDDu);
        system.writeMmioExplicit<u16>(transferAddrReg, 0x0002u);
        system.writeMmioExplicit<u16>(controlReg, kSpuControlStop);
        system.tickCpuCycles(kHandshakeDelayCycles);
        system.writeMmioExplicit<u32>(spuDmaBase + 0x0, 0x00014000u);
        system.writeMmioExplicit<u32>(spuDmaBase + 0x4, 0x00000001u);
        system.writeMmioExplicit<u32>(spuDmaBase + 0x8, 0x01000001u);
        require(system.spu().ramWords()[4] == 0u, "DMA write ran while SPU transfer mode was stop");
        system.writeMmioExplicit<u16>(controlReg, kSpuControlDmaWrite);
        system.tickCpuCycles(kHandshakeDelayCycles);
        system.writeMmioExplicit<u32>(spuDmaBase + 0x0, 0x00014000u);
        system.writeMmioExplicit<u32>(spuDmaBase + 0x4, 0x00000001u);
        system.writeMmioExplicit<u32>(spuDmaBase + 0x8, 0x01000001u);
        require(system.spu().ramWords()[4] == 0xAABBCCDDu,
                "DMA write did not respect the SPU transfer gate");
        system.writeMmioExplicit<u16>(controlReg, kSpuControlStop);
        system.tickCpuCycles(kHandshakeDelayCycles);
        system.writeMmioExplicit<u16>(transferAddrReg, 0x0002u);
        system.writeMmioExplicit<u16>(controlReg, kSpuControlDmaRead);
        system.tickCpuCycles(kHandshakeDelayCycles);
        system.tickCpuCycles(kHandshakeDelayCycles);
        system.write<psxrecomp::u32>(0x00014010u, 0u);
        system.writeMmioExplicit<u32>(spuDmaBase + 0x0, 0x00014010u);
        system.writeMmioExplicit<u32>(spuDmaBase + 0x4, 0x00000001u);
        system.writeMmioExplicit<u32>(spuDmaBase + 0x8, 0x01000000u);
        require(system.read<psxrecomp::u32>(0x00014010u) == 0xAABBCCDDu,
                "DMA read did not respect the SPU transfer gate");

        // IRQ address hit through RAM transfer and control-path acknowledge.
        const u32 spuLine = static_cast<u32>(InterruptLine::Spu);
        system.writeMmioExplicit<u16>(irqAddrReg, 0x0001u);
        system.writeMmioExplicit<u16>(transferAddrReg, 0x0001u);
        system.writeMmioExplicit<u16>(transferFifoReg, 0x5566u);
        system.writeMmioExplicit<u16>(controlReg, kSpuControlManualWriteIrq);
        system.tickCpuCycles(kHandshakeDelayCycles);
        require((system.readMmioExplicit<u16>(statusReg) & kStatusIrqFlag) != 0u,
                "IRQ-address hit did not latch SPUSTAT IRQ flag");
        require((system.interrupts().readStatus() & spuLine) != 0u,
                "IRQ-address hit did not assert the SPU interrupt line");
        system.writeMmioExplicit<u16>(controlReg, kSpuControlStopNoIrq);
        require((system.readMmioExplicit<u16>(statusReg) & kStatusIrqFlag) == 0u,
                "SPU IRQ flag did not clear through the control acknowledge path");
        system.interrupts().writeStatus(~spuLine);
        system.serviceInterrupts();
        require((system.interrupts().readStatus() & spuLine) == 0u,
                "cleared SPU IRQ reasserted unexpectedly");
    }

    {
        // ENDX and ADPCM loop-start/end semantics.
        Spu spu;
        spu.reset();

        writeAdpcmBlock(spu, 0x0000u, 0x01u, 0x11u);
        writeAdpcmBlock(spu, 0x0002u, 0x04u, 0x22u);
        writeAdpcmBlock(spu, 0x0004u, 0x03u, 0x33u);

        spu.writeRegister(0x000u, 0x3FFFu);
        spu.writeRegister(0x002u, 0x3FFFu);
        spu.writeRegister(0x004u, 0x1000u);
        spu.writeRegister(0x006u, 0x0000u);
        spu.writeRegister(0x00Eu, 0x0006u);

        spu.writeRegister(0x010u, 0x3FFFu);
        spu.writeRegister(0x012u, 0x3FFFu);
        spu.writeRegister(0x014u, 0x1000u);
        spu.writeRegister(0x016u, 0x0002u);
        spu.writeRegister(0x01Eu, 0x0010u);

        spu.writeRegister(Spu::RegisterMap::KeyOnLow, 0x0003u);
        require((spu.readRegister(Spu::RegisterMap::EndxLow) & 0x0003u) == 0u,
                "KON did not clear ENDX");

        spu.tick(28u * kCyclesPerSample);
        require((spu.readRegister(Spu::RegisterMap::EndxLow) & 0x0001u) != 0u,
                "loop-end did not set ENDX for the one-shot voice");
        require((spu.readRegister(Spu::RegisterMap::EndxLow) & 0x0002u) == 0u,
                "looping voice reached ENDX too early");
        require(spu.voices()[0].envelopePhase == Spu::Voice::EnvelopePhase::Off,
                "one-shot loop-end did not mute the voice");
        require(!spu.voices()[0].isActive, "one-shot loop-end left the voice active");
        require(spu.voices()[1].repeatAddress == 0x0002u,
                "loop-start did not capture the repeat address");
        require(spu.voices()[1].currentAddress == 0x0004u,
                "voice did not advance to the block after loop-start");

        spu.tick(28u * kCyclesPerSample);
        require((spu.readRegister(Spu::RegisterMap::EndxLow) & 0x0003u) == 0x0003u,
                "ENDX bits did not latch for both completed voices");
        require(spu.voices()[1].isActive, "end+repeat did not keep the looping voice active");
        if (spu.voices()[1].currentAddress != 0x0002u)
        {
            failValue("end+repeat did not jump back to the repeat address",
                      spu.voices()[1].currentAddress);
        }
    }

    return 0;
}
