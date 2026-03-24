/**
 * @file irq_edge_latching_test.cpp
 * @brief Regression tests for PSX-SPX IRQ edge-detection semantics.
 *
 * I_STAT bits must latch only on the false→true transition of the device
 * request line. Clearing I_STAT while the device source remains asserted
 * must NOT immediately recreate the bit. The bit only reappears when the
 * device drops and raises its line again (a new edge).
 *
 * Tests use real GPU and OTC-DMA device sources rather than direct
 * InterruptController::raise() calls so that the syncLevelInterruptSources()
 * edge-tracking layer is exercised end-to-end.
 */
#include "psxrecomp/runtime/dma.h"
#include "psxrecomp/runtime/memory_map.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cstdlib>
#include <iostream>

using psxrecomp::Address;
using psxrecomp::u32;
using psxrecomp::runtime::DmaController;
using psxrecomp::runtime::DmaPort;
using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::PsxSystem;
namespace Mmio = psxrecomp::runtime::Mmio;

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

constexpr Address channelBase(DmaPort port)
{
    return DmaController::ChannelBase + DmaController::ChannelStride * static_cast<Address>(port);
}

// Trigger an OTC DMA transfer and return the CHCR address for the channel.
// DICR must already have the OTC channel (6) enable bit set before calling.
Address triggerOtcDma(PsxSystem& system, Address dest, u32 wordCount)
{
    const Address base = channelBase(DmaPort::Otc);
    system.writeMmioExplicit<u32>(base + 0x0, dest);
    system.writeMmioExplicit<u32>(base + 0x4, wordCount);
    // CHCR: SyncMode=0 (manual), direction=to-RAM (bit0=0), bit24+bit28 start.
    system.writeMmioExplicit<u32>(base + 0x8, (1u << 24u) | (1u << 28u));
    return base + 0x8;
}

// ---------------------------------------------------------------
// Test 1: GPU IRQ edge latching via real GP0 command
//
// Writing 0x1F000000 to GP0 fires the GPU IRQ request line.
// syncLevelInterruptSources (called from MMIO write path and
// serviceInterrupts) must latch I_STAT.Gpu exactly once.
// Clearing I_STAT.Gpu while the source remains asserted must NOT
// recreate the bit; only a new edge (GP1 reset + GP0 again) relaches it.
// ---------------------------------------------------------------
static void testGpuEdgeLatching()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for GPU edge test");

    const u32 gpuLine = static_cast<u32>(InterruptLine::Gpu);
    system.interrupts().writeMask(gpuLine);

    // GPU IRQ must start clear.
    require((system.interrupts().readStatus() & gpuLine) == 0u,
            "I_STAT.Gpu should be clear before any GPU command");

    // GP0 command 0x1F: fire GPU IRQ.  The MMIO write triggers
    // syncLevelInterruptSources() which detects the false→true edge.
    system.writeMmioExplicit<u32>(Mmio::GPU_GP0, 0x1F000000u);
    require((system.interrupts().readStatus() & gpuLine) != 0u,
            "I_STAT.Gpu should be set after GP0 IRQ command");

    // Software ack: clear I_STAT.Gpu (W0C — write 0 to the bit).
    system.interrupts().writeStatus(~gpuLine);
    require((system.interrupts().readStatus() & gpuLine) == 0u,
            "I_STAT.Gpu should be clear immediately after software ack");

    // Source is still asserted.  serviceInterrupts calls syncLevelInterruptSources;
    // with edge tracking prev=true so the bit must NOT reappear.
    system.serviceInterrupts();
    require((system.interrupts().readStatus() & gpuLine) == 0u,
            "I_STAT.Gpu must not reassert while GPU source remains high (edge tracking)");

    // GP1 command 0x02 (reset GPU): this deasserts the GPU IRQ line.
    // Then GP0 0x1F again: new false→true edge → I_STAT.Gpu latches again.
    system.writeMmioExplicit<u32>(Mmio::GPU_GP1, 0x02000000u);
    system.writeMmioExplicit<u32>(Mmio::GPU_GP0, 0x1F000000u);
    require((system.interrupts().readStatus() & gpuLine) != 0u,
            "I_STAT.Gpu must latch again after source drops and rises (new edge)");

    std::cerr
        << "[PASS] GPU IRQ edge latching: clear-while-high no re-raise, new edge re-latches\n";
}

// ---------------------------------------------------------------
// Test 2: DMA IRQ edge latching via real OTC completion
//
// Running an OTC DMA with DICR channel-enable set raises the DICR master
// flag, which is the DMA IRQ source.  syncLevelInterruptSources detects
// the false→true edge and sets I_STAT.Dma.  Clearing I_STAT.Dma while
// the master flag remains high must not recreate the bit.  Only after
// W1C on the DICR flag (master flag drops) and a new DMA completion does
// I_STAT.Dma reappear.
// ---------------------------------------------------------------
static void testDmaIrqEdgeLatching()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for DMA edge test");

    const u32 dmaLine = static_cast<u32>(InterruptLine::Dma);
    system.interrupts().writeMask(dmaLine);

    // Enable OTC channel (6) in DPCR (bit 27 = ch6 master-enable).
    constexpr u32 dpcrEnableOtc = 1u << 27u;
    system.writeMmioExplicit<u32>(DmaController::ControlReg, dpcrEnableOtc);

    // Enable DICR channel-6 completion flag (bit 22) + master-enable (bit 23).
    constexpr u32 enableCh6 = 1u << 22u;
    constexpr u32 masterEn = 1u << 23u;
    system.writeMmioExplicit<u32>(DmaController::InterruptReg, enableCh6 | masterEn);

    // DMA IRQ must start clear.
    require((system.interrupts().readStatus() & dmaLine) == 0u,
            "I_STAT.Dma should be clear before any DMA transfer");

    // Trigger OTC DMA → completion flag latches → DICR master flag rises →
    // syncLevelInterruptSources detects edge → I_STAT.Dma set.
    constexpr Address dest1 = 0x00010000u;
    triggerOtcDma(system, dest1, 4u);
    require((system.interrupts().readStatus() & dmaLine) != 0u,
            "I_STAT.Dma should be set after OTC DMA completion");

    // Software ack: clear I_STAT.Dma.
    system.interrupts().writeStatus(~dmaLine);
    require((system.interrupts().readStatus() & dmaLine) == 0u,
            "I_STAT.Dma should be clear after software ack");

    // DICR master flag still high (channel-6 flag still set).
    // serviceInterrupts calls syncLevelInterruptSources; prev=true → no re-raise.
    system.serviceInterrupts();
    require((system.interrupts().readStatus() & dmaLine) == 0u,
            "I_STAT.Dma must not reassert while DICR master flag stays high (edge tracking)");

    // W1C: clear the channel-6 completion flag (bit 30 = flag for ch6).
    // This drops the DICR master flag → m_dma.irqRequested() becomes false.
    constexpr u32 flagCh6 = 1u << 30u; // flag bit = 24 + channel index 6
    const u32 dicrCurrent = system.read<u32>(DmaController::InterruptReg);
    system.writeMmioExplicit<u32>(DmaController::InterruptReg, dicrCurrent | flagCh6);

    // After the MMIO write, syncLevelInterruptSources runs and updates prev=false.
    require((system.interrupts().readStatus() & dmaLine) == 0u,
            "I_STAT.Dma should stay clear after DICR flag W1C while no new transfer");

    // New OTC DMA → DICR master flag rises again → new false→true edge → I_STAT.Dma set.
    constexpr Address dest2 = 0x00018000u;
    triggerOtcDma(system, dest2, 4u);
    require((system.interrupts().readStatus() & dmaLine) != 0u,
            "I_STAT.Dma must latch after new DMA completion following DICR flag clear");

    std::cerr
        << "[PASS] DMA IRQ edge latching: clear-while-high no re-raise, new edge re-latches\n";
}

} // namespace

int main()
{
    testGpuEdgeLatching();
    testDmaIrqEdgeLatching();

    std::cerr << "\nAll IRQ edge latching tests passed.\n";
    return 0;
}
