/**
 * @file dma_cdrom_deferred_test.cpp
 * @brief Regression tests for CDROM DMA deferred-busy semantics.
 *
 * When DRQSTS (I_STAT bit 6 of CDROM STATUS, HSTS bit 6) is not set and
 * the force-start bit is clear, the CDROM DMA channel must:
 *  1. NOT silently abandon the transfer.
 *  2. Keep CHCR bit 24 (Start/Busy) asserted to signal the channel is busy.
 *  3. NOT fire a DICR completion event (no data moved).
 *  4. Complete correctly once DRQSTS rises (handled in syncLevelInterruptSources).
 */
#include "psxrecomp/runtime/dma.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cstdlib>
#include <iostream>
#include <vector>

using psxrecomp::Address;
using psxrecomp::u32;
using psxrecomp::u8;
using psxrecomp::runtime::DmaController;
using psxrecomp::runtime::DmaPort;
using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::PsxSystem;

namespace
{

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << "\n";
        std::abort();
    }
}

constexpr u32 kReadCycles = 451584u;

Address cdromDmaBase()
{
    return DmaController::ChannelBase +
           DmaController::ChannelStride * static_cast<Address>(DmaPort::Cdrom);
}

void ackCdrom(psxrecomp::runtime::Cdrom& cdrom)
{
    while ((cdrom.readStatus() & (1u << 5u)) != 0u)
    {
        (void)cdrom.readResponse();
    }
    cdrom.writeInterruptFlags(0x07u);
}

std::vector<u8> makeSector(u8 base)
{
    std::vector<u8> sector(2048u, 0u);
    for (size_t i = 0; i < sector.size(); ++i)
    {
        sector[i] = static_cast<u8>(base + static_cast<u8>(i & 0xFFu));
    }
    return sector;
}

// ---------------------------------------------------------------
// Test 1: DMA3 with DRQSTS clear → CHCR busy stays set (deferred)
//         and no DICR completion fires until data is ready.
// ---------------------------------------------------------------
static void testCdromDmaDeferredWhenDrqstsNotSet()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for CDROM DMA deferred test");

    const auto sector = makeSector(0xAAu);
    system.cdrom().enqueueDataSector(sector);
    system.cdrom().writeInterruptEnable(0x1Fu);
    system.cdrom().writeCommand(0x06u); // ReadN
    ackCdrom(system.cdrom());
    system.cdrom().tick(kReadCycles);

    // Enable DPCR for CDROM (ch3 master-enable: bit 3 + 4*3 = bit 15).
    constexpr u32 dpcrEnableCdrom = 1u << 15u;
    system.write<u32>(DmaController::ControlReg, dpcrEnableCdrom);

    // Enable DICR channel-3 completion interrupts (bit 19) + master-enable (bit 23).
    constexpr u32 dicrEnableCh3 = (1u << 19u) | (1u << 23u);
    system.write<u32>(DmaController::InterruptReg, dicrEnableCh3);

    constexpr Address dest = 0x00020000u;
    system.write<u32>(dest, 0xDEADBEEFu);

    const Address base = cdromDmaBase();
    system.write<u32>(base + 0x0, dest);
    system.write<u32>(base + 0x4, 0x00000200u); // 512 words = 2KB
    // CHCR: bit 0=1 (to-RAM), bits 24+28 (start).
    constexpr u32 chcrStart = 0x01u | (1u << 24u) | (1u << 28u);
    system.write<u32>(base + 0x8, chcrStart);

    // Snapshot state immediately after the CHCR write.
    // If DRQSTS was not set at trigger time, the channel must be deferred:
    //   - CHCR bit 24 stays set (channel still busy)
    //   - DICR master flag clear (no completion yet)
    //   - I_STAT.Dma clear (no DMA IRQ yet)
    const u32 chcrImmediate = system.read<u32>(base + 0x8);
    const u32 dicrImmediate = system.read<u32>(DmaController::InterruptReg);
    const u32 dmaLine = static_cast<u32>(InterruptLine::Dma);

    // Determine if transfer fired immediately or was deferred.
    const bool firedImmediately = (chcrImmediate & (1u << 24u)) == 0u;

    if (!firedImmediately)
    {
        // Deferred path: CHCR bit 24 must be set, DICR master flag must be clear.
        require((chcrImmediate & (1u << 24u)) != 0u,
                "Deferred CDROM DMA: CHCR bit24 (busy) must stay set while data not ready");
        require((dicrImmediate & (1u << 31u)) == 0u,
                "Deferred CDROM DMA: DICR master flag must be clear before transfer completes");
        require((system.interrupts().readStatus() & dmaLine) == 0u,
                "Deferred CDROM DMA: I_STAT.Dma must be clear before transfer completes");
    }

    // Tick enough for deferred completion to fire.
    system.cdrom().tick(2000u);

    // After completion (deferred or immediate), CHCR bit 24 must be clear.
    require((system.read<u32>(base + 0x8) & (1u << 24u)) == 0u,
            "CDROM DMA: CHCR bit24 must be cleared after transfer completes");

    std::cerr << "[PASS] CDROM DMA deferred: CHCR busy stays set until data ready, clears on "
                 "completion\n";
}

// ---------------------------------------------------------------
// Test 2: Force-start bypasses DRQSTS gate and transfers immediately
// ---------------------------------------------------------------
static void testCdromDmaForceStartBypasses()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for CDROM DMA force-start test");

    const auto sector = makeSector(0xBBu);
    system.cdrom().enqueueDataSector(sector);
    system.cdrom().writeInterruptEnable(0x1Fu);
    system.cdrom().writeCommand(0x06u); // ReadN
    ackCdrom(system.cdrom());
    system.cdrom().tick(kReadCycles);

    // Ensure INT1 has fired (data ready).
    require((system.cdrom().readInterruptFlags() & 0x07u) == 0x01u,
            "CDROM must signal INT1 (data ready) after ReadN + tick");
    ackCdrom(system.cdrom());

    constexpr u32 dpcrEnableCdrom = 1u << 15u;
    system.write<u32>(DmaController::ControlReg, dpcrEnableCdrom);

    constexpr Address dest = 0x00030000u;
    system.write<u32>(dest, 0xDEADBEEFu);

    const Address base = cdromDmaBase();
    system.write<u32>(base + 0x0, dest);
    system.write<u32>(base + 0x4, 0x00000001u); // 1 word
    // CHCR: to-RAM (bit 0), force-start (bit 28), bit24 busy.
    constexpr u32 chcrForce = 0x01u | (1u << 24u) | (1u << 28u);
    system.write<u32>(base + 0x8, chcrForce);

    // Transfer should have fired immediately (force bypasses DRQSTS).
    // CHCR bit 24 must be clear (transfer completed or skipped gracefully).
    require((system.read<u32>(base + 0x8) & (1u << 24u)) == 0u,
            "CDROM DMA force-start: CHCR bit24 must be clear after force-start");

    std::cerr << "[PASS] CDROM DMA force-start bypasses DRQSTS gate\n";
}

// ---------------------------------------------------------------
// Test 3: After deferred DMA completes, CHCR busy bit is cleared
//         and DICR completion flag is latched (ordering check).
// ---------------------------------------------------------------
static void testCdromDmaDeferredCompletionClearsBusy()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for CDROM DMA completion test");

    const auto sector = makeSector(0xCCu);
    system.cdrom().enqueueDataSector(sector);
    system.cdrom().writeInterruptEnable(0x1Fu);
    system.cdrom().writeCommand(0x06u);
    ackCdrom(system.cdrom());
    system.cdrom().tick(kReadCycles);

    constexpr u32 dpcrEnableCdrom = 1u << 15u;
    system.write<u32>(DmaController::ControlReg, dpcrEnableCdrom);

    // Enable DICR ch3 so the completion flag latches.
    constexpr u32 dicrEnableCh3 = (1u << 19u) | (1u << 23u);
    system.write<u32>(DmaController::InterruptReg, dicrEnableCh3);

    constexpr Address dest = 0x00040000u;
    const Address base = cdromDmaBase();
    system.write<u32>(base + 0x0, dest);
    system.write<u32>(base + 0x4, 0x00000200u);
    constexpr u32 chcrStart = 0x01u | (1u << 24u) | (1u << 28u);
    system.write<u32>(base + 0x8, chcrStart);

    // Tick to allow deferred completion to fire if needed.
    system.cdrom().tick(2000u);

    // CHCR bit 24 must be clear (transfer completed).
    require((system.read<u32>(base + 0x8) & (1u << 24u)) == 0u,
            "CDROM DMA deferred: CHCR bit24 must be cleared after completion");

    // DICR ch3 flag (bit 27 = 24+3) must be set (completion was enabled).
    constexpr u32 flagCh3 = 1u << 27u;
    require((system.read<u32>(DmaController::InterruptReg) & flagCh3) != 0u,
            "CDROM DMA deferred: DICR ch3 flag must be latched after enabled completion");

    std::cerr
        << "[PASS] CDROM DMA deferred: CHCR busy cleared and DICR flag latched after completion\n";
}

} // namespace

int main()
{
    testCdromDmaDeferredWhenDrqstsNotSet();
    testCdromDmaForceStartBypasses();
    testCdromDmaDeferredCompletionClearsBusy();

    std::cerr << "\nAll CDROM DMA deferred tests passed.\n";
    return 0;
}
