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

[[noreturn]] void failValue(const char* message, u32 value)
{
    std::ostringstream stream;
    stream << message << " value=0x" << std::hex << value;
    throw std::runtime_error(stream.str());
}
} // namespace

int main()
{
    PsxSystem system;
    require(system.initialize(), "failed to initialize SPU transfer test system");

    const Address transferAddrReg = spuRegisterAddress(Spu::RegisterMap::RamTransferAddress);
    const Address transferFifoReg = spuRegisterAddress(Spu::RegisterMap::RamTransferData);
    const Address transferCtrlReg = spuRegisterAddress(Spu::RegisterMap::TransferControl);
    const Address controlReg = spuRegisterAddress(Spu::RegisterMap::Control);
    const Address statusReg = spuRegisterAddress(Spu::RegisterMap::Status);
    const Address spuDmaBase = DmaController::ChannelBase +
                               DmaController::ChannelStride * static_cast<Address>(DmaPort::Spu);

    system.writeMmioExplicit<u16>(transferCtrlReg, 0x0004u);

    // DA6 is in 8-byte units; manual-write uses internal current address and must
    // leave the visible register unchanged.
    system.writeMmioExplicit<u16>(controlReg, 0x0000u);
    system.tickCpuCycles(kHandshakeDelayCycles);
    system.writeMmioExplicit<u16>(transferAddrReg, 0x0001u);
    system.writeMmioExplicit<u16>(transferFifoReg, 0x1122u);
    system.writeMmioExplicit<u16>(transferFifoReg, 0x3344u);
    system.writeMmioExplicit<u16>(controlReg, 0x0010u);
    system.tickCpuCycles(kHandshakeDelayCycles);
    require((system.readMmioExplicit<u16>(statusReg) & (1u << 10)) != 0u,
            "manual transfer busy should assert");
    system.tickCpuCycles(kHandshakeDelayCycles * 2u);
    require(system.spu().ramWords()[2] == 0x33441122u,
            "manual transfer did not land at byte address 8");
    require(system.spu().ramWords()[0] == 0u && system.spu().ramWords()[1] == 0u,
            "manual transfer used wrong DA6 units");
    require(system.readMmioExplicit<u16>(transferAddrReg) == 0x0001u,
            "visible transfer address changed during manual transfer");

    // DMA write must be gated by the current transfer mode.
    system.write<psxrecomp::u32>(0x00014000u, 0xAABBCCDDu);
    system.writeMmioExplicit<u16>(transferAddrReg, 0x0002u);
    system.writeMmioExplicit<u16>(controlReg, 0x0000u);
    system.tickCpuCycles(kHandshakeDelayCycles);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x0, 0x00014000u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x4, 0x00000001u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x8,
                                  0x11000001u); // SyncMode=0 + bit28 trigger, RAM→SPU
    require(system.spu().ramWords()[4] == 0u, "DMA write ran while SPU transfer mode was stop");

    system.writeMmioExplicit<u16>(controlReg, 0x0020u);
    system.tickCpuCycles(kHandshakeDelayCycles);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x0, 0x00014000u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x4, 0x00000001u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x8,
                                  0x11000001u); // SyncMode=0 + bit28 trigger, RAM→SPU
    require(system.spu().ramWords()[4] == 0xAABBCCDDu,
            "DMA write did not use modeled SPU transfer engine");
    require(system.readMmioExplicit<u16>(transferAddrReg) == 0x0002u,
            "visible transfer address changed during DMA write");

    // DMA read should read back through the same modeled current-address path.
    system.writeMmioExplicit<u16>(controlReg, 0x0000u);
    system.tickCpuCycles(kHandshakeDelayCycles);
    system.writeMmioExplicit<u16>(transferAddrReg, 0x0002u);
    system.writeMmioExplicit<u16>(controlReg, 0x0030u);
    system.tickCpuCycles(kHandshakeDelayCycles);
    system.tickCpuCycles(kHandshakeDelayCycles);
    require(system.spu().readDma() == 0xAABBCCDDu,
            "raw SPU DMA readback failed after resetting transfer address");
    system.writeMmioExplicit<u16>(transferAddrReg, 0x0002u);
    system.write<psxrecomp::u32>(0x00014010u, 0u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x0, 0x00014010u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x4, 0x00000001u);
    system.writeMmioExplicit<u32>(spuDmaBase + 0x8,
                                  0x11000000u); // SyncMode=0 + bit28 trigger, SPU→RAM
    const u32 dmaReadBack = system.read<psxrecomp::u32>(0x00014010u);
    if (dmaReadBack != 0xAABBCCDDu)
    {
        failValue("DMA read did not use modeled SPU transfer engine", dmaReadBack);
    }
    require(system.readMmioExplicit<u16>(transferAddrReg) == 0x0002u,
            "visible transfer address changed during DMA read");

    return 0;
}
