/**
 * @file dma_dicr_semantics_test.cpp
 * @brief Regression tests for PSX-SPX DMA DICR semantics.
 *
 * Verified:
 *  1. Per-channel completion flag latches only when the enable bit (16+N) is set.
 *  2. Completion flag does NOT latch when enable bit is clear.
 *  3. Clearing I_STAT.3 (DMA IRQ) while DICR master flag remains high does not
 *     recreate IRQ3 until the source drops (DICR W1C) and rises again (new edge).
 *  4. DICR master flag (bit 31) is read-only computed: set when master-enable
 *     (bit 23) AND any channel flag (bits 24-30) are both set.
 *  5. DMA IRQ edge: I_STAT.Dma driven by real OTC completion; clear while DICR
 *     high → no re-raise; W1C on flag → new transfer → I_STAT.Dma reappears.
 */
#include "psxrecomp/runtime/dma.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <cstdlib>
#include <iostream>

using psxrecomp::Address;
using psxrecomp::u32;
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

constexpr Address channelBase(DmaPort port)
{
    return DmaController::ChannelBase + DmaController::ChannelStride * static_cast<Address>(port);
}

// ---------------------------------------------------------------
// Test 1: Channel flag latches when enable bit is set
// ---------------------------------------------------------------
static void testFlagLatchesWhenEnabled()
{
    DmaController dma;
    dma.reset();

    // Write DICR: enable channel 4 (SPU, bit 20) + master-enable (bit 23).
    constexpr u32 enableBit = 1u << 20u; // channel 4 enable
    constexpr u32 masterEn = 1u << 23u;
    const u32 dicr = enableBit | masterEn;
    dma.writeRegister(DmaController::InterruptReg, dicr);

    // Simulate transfer complete on channel 4.
    dma.notifyTransferComplete(DmaPort::Spu);

    // Channel 4 flag (bit 28) should now be set.
    const u32 dicrAfter = dma.readRegister(DmaController::InterruptReg);
    constexpr u32 flagBit = 1u << 28u;
    assert((dicrAfter & flagBit) != 0u);

    // irqRequested() should be true: master-enable AND flag are both set.
    assert(dma.irqRequested());

    std::cerr << "[PASS] Channel flag latches when enable bit is set\n";
}

// ---------------------------------------------------------------
// Test 2: Channel flag does NOT latch when enable bit is clear
// ---------------------------------------------------------------
static void testFlagDoesNotLatchWhenDisabled()
{
    DmaController dma;
    dma.reset();

    // Write DICR: master-enable set but channel 4 enable bit CLEAR.
    constexpr u32 masterEn = 1u << 23u;
    dma.writeRegister(DmaController::InterruptReg, masterEn);

    dma.notifyTransferComplete(DmaPort::Spu);

    // Channel 4 flag (bit 28) must NOT be set.
    const u32 dicrAfter = dma.readRegister(DmaController::InterruptReg);
    constexpr u32 flagBit = 1u << 28u;
    assert((dicrAfter & flagBit) == 0u);

    // No IRQ either.
    assert(!dma.irqRequested());

    std::cerr << "[PASS] Channel flag does not latch when enable bit is clear\n";
}

// ---------------------------------------------------------------
// Test 3: DICR W1C — writing 1 to a flag bit clears it
// ---------------------------------------------------------------
static void testDicrFlagW1CClear()
{
    DmaController dma;
    dma.reset();

    // Enable ch4, trigger completion to set the flag.
    constexpr u32 enableBit = 1u << 20u;
    constexpr u32 masterEn = 1u << 23u;
    dma.writeRegister(DmaController::InterruptReg, enableBit | masterEn);
    dma.notifyTransferComplete(DmaPort::Spu);

    constexpr u32 flagBit = 1u << 28u;
    assert((dma.readRegister(DmaController::InterruptReg) & flagBit) != 0u);

    // Write 1 to bit 28 to clear it (W1C).
    const u32 current = dma.readRegister(DmaController::InterruptReg);
    dma.writeRegister(DmaController::InterruptReg, current | flagBit);
    assert((dma.readRegister(DmaController::InterruptReg) & flagBit) == 0u);

    std::cerr << "[PASS] DICR W1C: writing 1 to flag bit clears it\n";
}

// ---------------------------------------------------------------
// Test 4: Multiple channels — only enabled channels latch flags
// ---------------------------------------------------------------
static void testMultiChannelSelectiveLatch()
{
    DmaController dma;
    dma.reset();

    // Enable ch2 (GPU, bit 18) and ch4 (SPU, bit 20); master-enable.
    constexpr u32 enableCh2 = 1u << 18u;
    constexpr u32 enableCh4 = 1u << 20u;
    constexpr u32 masterEn = 1u << 23u;
    dma.writeRegister(DmaController::InterruptReg, enableCh2 | enableCh4 | masterEn);

    // Complete ch2 (GPU) and ch3 (CDROM — NOT enabled).
    dma.notifyTransferComplete(DmaPort::Gpu);
    dma.notifyTransferComplete(DmaPort::Cdrom);

    const u32 dicrAfter = dma.readRegister(DmaController::InterruptReg);
    constexpr u32 flagCh2 = 1u << 26u;   // bit 24+2
    constexpr u32 flagCh3 = 1u << 27u;   // bit 24+3
    assert((dicrAfter & flagCh2) != 0u); // GPU was enabled → flag set
    assert((dicrAfter & flagCh3) == 0u); // CDROM was NOT enabled → flag clear

    std::cerr << "[PASS] Multi-channel: only enabled channels latch flags\n";
}

// ---------------------------------------------------------------
// Test 5: DMA IRQ edge: real OTC completion drives DICR master flag
//
// Run OTC DMA with ch6 enable set → DICR master flag rises → I_STAT.Dma
// latched.  Clear I_STAT.Dma (software ack): master flag stays high.
// serviceInterrupts must NOT re-raise I_STAT.Dma (edge tracking: prev=true).
// W1C on DICR ch6 flag → master flag falls → prev resets to false.
// New OTC transfer → DICR master flag rises again → I_STAT.Dma re-latches.
// ---------------------------------------------------------------
static void testDmaIrqEdgeNoClearRecreate()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for DMA IRQ edge test");

    constexpr u32 dmaBit = static_cast<u32>(InterruptLine::Dma);
    system.interrupts().writeMask(dmaBit);

    // Enable OTC channel (6) in DPCR (bit 27 = ch6 master-enable).
    constexpr u32 dpcrEnableOtc = 1u << 27u;
    system.writeMmioExplicit<u32>(DmaController::ControlReg, dpcrEnableOtc);

    // Enable DICR channel-6 completion flag (bit 22) + master-enable (bit 23).
    constexpr u32 enableCh6 = 1u << 22u;
    constexpr u32 masterEn = 1u << 23u;
    system.writeMmioExplicit<u32>(DmaController::InterruptReg, enableCh6 | masterEn);

    require((system.interrupts().readStatus() & dmaBit) == 0u,
            "I_STAT.Dma should be clear before any transfer");

    // Phase 1: run OTC DMA → DICR flag latches → master flag rises → I_STAT.Dma set.
    const Address base = channelBase(DmaPort::Otc);
    system.writeMmioExplicit<u32>(base + 0x0, 0x00010000u);
    system.writeMmioExplicit<u32>(base + 0x4, 4u);
    system.writeMmioExplicit<u32>(base + 0x8, (1u << 24u) | (1u << 28u));

    require((system.interrupts().readStatus() & dmaBit) != 0u,
            "I_STAT.Dma should be set after OTC DMA completion");

    // Software ack.
    system.interrupts().writeStatus(~dmaBit);
    require((system.interrupts().readStatus() & dmaBit) == 0u,
            "I_STAT.Dma should be clear after software ack");

    // DICR master flag still high; edge tracking (prev=true) must suppress re-raise.
    system.serviceInterrupts();
    require((system.interrupts().readStatus() & dmaBit) == 0u,
            "I_STAT.Dma must not reassert while DICR master flag stays high");

    // W1C: clear ch6 flag (bit 30 = 24 + 6) → DICR master flag falls → prev resets.
    constexpr u32 flagCh6 = 1u << 30u;
    const u32 dicrNow = system.read<u32>(DmaController::InterruptReg);
    system.writeMmioExplicit<u32>(DmaController::InterruptReg, dicrNow | flagCh6);

    require((system.interrupts().readStatus() & dmaBit) == 0u,
            "I_STAT.Dma should stay clear after DICR flag W1C");

    // Phase 2: new OTC DMA → DICR master flag rises → new false→true edge → re-latch.
    system.writeMmioExplicit<u32>(base + 0x0, 0x00018000u);
    system.writeMmioExplicit<u32>(base + 0x4, 4u);
    system.writeMmioExplicit<u32>(base + 0x8, (1u << 24u) | (1u << 28u));

    require((system.interrupts().readStatus() & dmaBit) != 0u,
            "I_STAT.Dma must re-latch after new OTC completion following DICR flag clear");

    std::cerr << "[PASS] DMA IRQ edge: real OTC source, clear-while-high no re-raise, new edge "
                 "re-latches\n";
}

} // namespace

int main()
{
    testFlagLatchesWhenEnabled();
    testFlagDoesNotLatchWhenDisabled();
    testDicrFlagW1CClear();
    testMultiChannelSelectiveLatch();
    testDmaIrqEdgeNoClearRecreate();

    std::cerr << "\nAll DMA DICR semantics tests passed.\n";
    return 0;
}
