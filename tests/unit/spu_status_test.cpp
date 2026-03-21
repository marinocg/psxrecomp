#include "psxrecomp/runtime/psx_system.h"

#include <sstream>
#include <stdexcept>

namespace
{
using psxrecomp::Address;
using psxrecomp::u16;
using psxrecomp::u32;
using psxrecomp::runtime::DmaController;
using psxrecomp::runtime::DmaPort;
using psxrecomp::runtime::PsxSystem;
using psxrecomp::runtime::Spu;

constexpr u16 kStatusIrqFlag = 1u << 6;
constexpr u16 kStatusDmaRequest = 1u << 7;
constexpr u16 kStatusDmaWriteRequest = 1u << 8;
constexpr u16 kStatusDmaReadRequest = 1u << 9;
constexpr u16 kStatusBusy = 1u << 10;
constexpr u32 kHandshakeDelayCycles = 0x300u;

Address spuRegisterAddress(u32 rawOffset)
{
    return psxrecomp::runtime::Mmio::SPU_BASE + (rawOffset & 0x1FFu);
}

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

[[noreturn]] void failStatus(const char* message, u16 status)
{
    std::ostringstream stream;
    stream << message << " status=0x" << std::hex << status;
    throw std::runtime_error(stream.str());
}
} // namespace

int main()
{
    PsxSystem system;
    require(system.initialize(), "failed to initialize SPU status test system");

    const Address controlReg = spuRegisterAddress(Spu::RegisterMap::Control);
    const Address statusReg = spuRegisterAddress(Spu::RegisterMap::Status);
    const Address transferAddrReg = spuRegisterAddress(Spu::RegisterMap::RamTransferAddress);
    const Address transferCtrlReg = spuRegisterAddress(Spu::RegisterMap::TransferControl);
    const Address spuDmaBase = DmaController::ChannelBase +
                               DmaController::ChannelStride * static_cast<Address>(DmaPort::Spu);

    // Clear boot state set by primeBootState() so we start from a clean SPUSTAT.
    system.writeMmioExplicit<u16>(controlReg, 0x0000u);
    system.tickCpuCycles(kHandshakeDelayCycles);
    const u16 initialStatus = system.readMmioExplicit<u16>(statusReg);
    require((initialStatus & 0x7FFu) == 0u, "SPUSTAT should be clear after control reset");

    system.writeMmioExplicit<u16>(controlReg, 0x0010u);
    require((system.readMmioExplicit<u16>(statusReg) & 0x3Fu) == 0u,
            "manual-write mode applied immediately");
    require((system.readMmioExplicit<u16>(statusReg) & kStatusBusy) != 0u,
            "SPUSTAT busy should assert while control bits are pending");

    system.tickCpuCycles(kHandshakeDelayCycles - 1u);
    require((system.readMmioExplicit<u16>(statusReg) & 0x3Fu) == 0u,
            "manual-write mode applied too early");

    system.tickCpuCycles(1u);
    require((system.readMmioExplicit<u16>(statusReg) & 0x3Fu) == 0x0010u,
            "manual-write mode did not appear after delay");
    require((system.readMmioExplicit<u16>(statusReg) & kStatusBusy) == 0u,
            "SPUSTAT busy did not clear after manual-write apply");

    system.writeMmioExplicit<u16>(statusReg, 0xFFFFu);
    const u16 statusAfterWrite = system.readMmioExplicit<u16>(statusReg);
    require((statusAfterWrite & kStatusIrqFlag) == 0u, "SPUSTAT write changed IRQ flag");
    require((statusAfterWrite & 0x3Fu) == 0x0010u, "SPUSTAT write changed applied mode bits");

    system.writeMmioExplicit<u16>(controlReg, 0x0020u);
    system.tickCpuCycles(kHandshakeDelayCycles);
    const u16 dmaWriteStatus = system.readMmioExplicit<u16>(statusReg);
    require((dmaWriteStatus & 0x3Fu) == 0x0020u, "DMA-write mode did not apply");
    require((dmaWriteStatus & kStatusDmaWriteRequest) != 0u, "DMA-write request bit missing");
    require((dmaWriteStatus & kStatusDmaRequest) != 0u, "DMA request summary missing");
    require((dmaWriteStatus & kStatusDmaReadRequest) == 0u,
            "DMA-read request should stay clear in DMA-write mode");

    system.writeMmioExplicit<u16>(controlReg, 0x0030u);
    system.tickCpuCycles(kHandshakeDelayCycles);
    const u16 dmaReadStatusPending = system.readMmioExplicit<u16>(statusReg);
    require((dmaReadStatusPending & 0x3Fu) == 0x0030u, "DMA-read mode did not apply");
    if ((dmaReadStatusPending & kStatusDmaReadRequest) != 0u)
    {
        failStatus("DMA-read request asserted without warmup", dmaReadStatusPending);
    }
    if ((dmaReadStatusPending & kStatusDmaRequest) != 0u)
    {
        failStatus("DMA summary asserted without DMA-read warmup", dmaReadStatusPending);
    }
    if ((dmaReadStatusPending & kStatusBusy) == 0u)
    {
        failStatus("busy should remain set during DMA-read warmup", dmaReadStatusPending);
    }

    system.tickCpuCycles(kHandshakeDelayCycles);
    const u16 dmaReadStatusReady = system.readMmioExplicit<u16>(statusReg);
    require((dmaReadStatusReady & kStatusDmaReadRequest) != 0u,
            "DMA-read request did not appear after warmup");
    require((dmaReadStatusReady & kStatusDmaRequest) != 0u,
            "DMA summary missing after DMA-read warmup");
    require((dmaReadStatusReady & kStatusDmaWriteRequest) == 0u,
            "DMA-write request should stay clear in DMA-read mode");

    system.writeMmioExplicit<u16>(transferCtrlReg, 0x0004u);
    system.writeMmioExplicit<u16>(transferAddrReg, 0u);
    system.spu().writeDma(0x11223344u);
    system.writeMmioExplicit<u16>(transferAddrReg, 0u);
    require(system.spu().readDma() == 0x11223344u, "direct SPU DMA readback failed");
    system.writeMmioExplicit<u16>(transferAddrReg, 0u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x0, 0x00012000u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x4, 0x00000001u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x8, 0x01000000u);
    require(system.read<u32>(0x00012000u) == 0x11223344u, "SPU DMA-read did not reach RAM");
    require((system.readMmioExplicit<u16>(statusReg) & kStatusBusy) != 0u,
            "busy should assert after SPU DMA transfer");

    system.tickCpuCycles(kHandshakeDelayCycles * 2u);
    require((system.readMmioExplicit<u16>(statusReg) & kStatusBusy) == 0u,
            "busy did not clear after DMA transfer delay");

    system.writeMmioExplicit<u16>(controlReg, 0x0000u);
    system.tickCpuCycles(kHandshakeDelayCycles);
    require((system.readMmioExplicit<u16>(statusReg) & 0x3Fu) == 0u,
            "stop mode did not apply after delay");
    require((system.readMmioExplicit<u16>(statusReg) &
             (kStatusDmaRequest | kStatusDmaWriteRequest | kStatusDmaReadRequest)) == 0u,
            "DMA request bits remained set in stop mode");

    return 0;
}
