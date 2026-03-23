/**
 * @file wait_event_test.cpp
 * @brief Tests for blocking WaitEvent (B0:0A) semantics.
 *
 * Verifies that WaitEvent blocks until delivery, that TestEvent resets
 * delivered state correctly, and that invalid/disabled handles fail cleanly.
 */
#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <iostream>

namespace EventClass = psxrecomp::runtime::EventClass;
namespace EventSpec = psxrecomp::runtime::EventSpec;
using psxrecomp::u32;
using psxrecomp::runtime::DmaController;
using psxrecomp::runtime::DmaPort;
using psxrecomp::runtime::EventMode;
using psxrecomp::runtime::EventStatus;
using psxrecomp::runtime::KernelEventTable;
using psxrecomp::runtime::LogLevel;
using psxrecomp::runtime::PsxSystem;
using psxrecomp::runtime::Spu;

constexpr u32 kHandshakeDelayCycles = 0x300u;

static psxrecomp::Address spuRegisterAddress(u32 rawOffset)
{
    return psxrecomp::runtime::Mmio::SPU_BASE + (rawOffset & 0x1FFu);
}

// ---------------------------------------------------------------
// Helper: call WaitEvent via BIOS B0 vector.
// ---------------------------------------------------------------
static void callWaitEvent(PsxSystem& system, u32 handle, u32* regs)
{
    regs[9] = 0x0A; // $t1 = function id WaitEvent
    regs[4] = handle;
    system.observeProgramCounter(0x80016000u);
    system.callBiosVector(0xB0, regs, 32);
}

// ---------------------------------------------------------------
// Helper: call TestEvent via BIOS B0 vector.
// ---------------------------------------------------------------
static u32 callTestEvent(PsxSystem& system, u32 handle, u32* regs)
{
    regs[9] = 0x0B; // $t1 = function id TestEvent
    regs[4] = handle;
    system.callBiosVector(0xB0, regs, 32);
    return regs[2]; // $v0
}

// ---------------------------------------------------------------
// Test 1: Event already delivered — WaitEvent returns immediately.
// ---------------------------------------------------------------
static void testAlreadyDelivered()
{
    PsxSystem system;
    assert(system.initialize());

    u32 handle =
        system.events().openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
    assert(handle != 0xFFFFFFFFu);
    system.events().enableEvent(handle);
    system.events().deliverEvent(handle);
    assert(system.events().isEventDelivered(handle));

    u32 regs[32] = {};
    callWaitEvent(system, handle, regs);

    // Should have returned successfully without looping.
    assert(regs[2] == 1);
    assert(!system.events().isEventDelivered(handle));
    assert(system.events().getEvent(handle)->status == EventStatus::Enabled);
    std::cerr << "[PASS] WaitEvent returns immediately when already delivered\n";
}

// ---------------------------------------------------------------
// Test 2: Event delivered later by IRQ — WaitEvent blocks until
//         delivery is triggered by VBlank interrupt dispatch.
// ---------------------------------------------------------------
static void testDeliveredByIrq()
{
    PsxSystem system;
    assert(system.initialize());

    // Open a VBlank/Counter event in NoCallback mode.
    u32 handle =
        system.events().openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
    assert(handle != 0xFFFFFFFFu);
    system.events().enableEvent(handle);

    // The event should NOT be delivered yet.
    assert(!system.events().isEventDelivered(handle));

    // The system's interrupt dispatcher needs to know about VBlank→event
    // mapping. The boot() path and interrupt dispatcher already handle this.
    // We need the interrupt mask to include VBlank (boot() does this).
    system.observeProgramCounter(0x80016000u);

    u32 regs[32] = {};
    callWaitEvent(system, handle, regs);

    // WaitEvent should have blocked, ticked hardware until VBlank fired,
    // then the interrupt dispatcher delivered the VBlank event.
    assert(regs[2] == 1);
    assert(!system.events().isEventDelivered(handle));
    assert(system.events().getEvent(handle)->status == EventStatus::Enabled);
    std::cerr << "[PASS] WaitEvent blocks and resumes after VBlank delivery\n";
}

// ---------------------------------------------------------------
// Test 3: TestEvent resets delivered state correctly.
// ---------------------------------------------------------------
static void testTestEventResetsDelivery()
{
    PsxSystem system;
    assert(system.initialize());

    u32 handle =
        system.events().openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
    assert(handle != 0xFFFFFFFFu);
    system.events().enableEvent(handle);

    // Deliver the event.
    system.events().deliverEvent(handle);
    assert(system.events().isEventDelivered(handle));

    // TestEvent should return 1 and reset status to Enabled.
    u32 regs[32] = {};
    [[maybe_unused]] u32 result = callTestEvent(system, handle, regs);
    assert(result == 1);

    // After TestEvent, the event is back to Enabled, not Delivered.
    assert(!system.events().isEventDelivered(handle));
    assert(system.events().getEvent(handle)->status == EventStatus::Enabled);

    // A second TestEvent should return 0 (not yet delivered again).
    result = callTestEvent(system, handle, regs);
    assert(result == 0);

    std::cerr << "[PASS] TestEvent resets delivered state correctly\n";
}

// ---------------------------------------------------------------
// Test 4: WaitEvent on invalid handle returns false / 0.
// ---------------------------------------------------------------
static void testInvalidHandle()
{
    PsxSystem system;
    assert(system.initialize());

    u32 regs[32] = {};
    callWaitEvent(system, 0xDEAD0000u, regs);
    // Should not crash, return 0 for invalid.
    assert(regs[2] == 0);
    std::cerr << "[PASS] WaitEvent on invalid handle returns 0\n";
}

// ---------------------------------------------------------------
// Test 5: WaitEvent on disabled event returns false / 0.
// ---------------------------------------------------------------
static void testDisabledEvent()
{
    PsxSystem system;
    assert(system.initialize());

    u32 handle =
        system.events().openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
    assert(handle != 0xFFFFFFFFu);
    // Event is in Disabled state (default after OpenEvent).

    u32 regs[32] = {};
    callWaitEvent(system, handle, regs);
    assert(regs[2] == 0);
    std::cerr << "[PASS] WaitEvent on disabled event returns 0\n";
}

// ---------------------------------------------------------------
// Test 6: Callback-mode events — WaitEvent returns immediately.
// ---------------------------------------------------------------
static void testCallbackModeNonBlocking()
{
    PsxSystem system;
    assert(system.initialize());

    u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                           EventMode::Callback, 0x80010000u);
    assert(handle != 0xFFFFFFFFu);
    system.events().enableEvent(handle);

    u32 regs[32] = {};
    callWaitEvent(system, handle, regs);
    // Callback events: WaitEvent returns immediately.
    assert(regs[2] == 1);
    std::cerr << "[PASS] WaitEvent on callback-mode event returns immediately\n";
}

// ---------------------------------------------------------------
// Test 7: UnDeliverEvent resets delivery, subsequent WaitEvent blocks.
// ---------------------------------------------------------------
static void testUnDeliverThenWaitBlocks()
{
    PsxSystem system;
    assert(system.initialize());

    u32 handle =
        system.events().openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
    assert(handle != 0xFFFFFFFFu);
    system.events().enableEvent(handle);
    system.events().deliverEvent(handle);
    assert(system.events().isEventDelivered(handle));

    // UnDeliver resets back to Enabled.
    system.events().undeliverEvent(handle);
    assert(!system.events().isEventDelivered(handle));
    assert(system.events().getEvent(handle)->status == EventStatus::Enabled);

    // Now WaitEvent should block until re-delivery via VBlank IRQ.
    u32 regs[32] = {};
    callWaitEvent(system, handle, regs);
    assert(regs[2] == 1);
    std::cerr << "[PASS] UnDeliverEvent resets then WaitEvent blocks until re-delivery\n";
}

// ---------------------------------------------------------------
// Test 8: SPU completion event only fires for real DMA4 transfers.
// ---------------------------------------------------------------
static void testSpuWaitEventTracksRealDmaCompletion()
{
    PsxSystem system;
    assert(system.initialize());

    const psxrecomp::Address controlReg = spuRegisterAddress(Spu::RegisterMap::Control);
    const psxrecomp::Address transferAddrReg =
        spuRegisterAddress(Spu::RegisterMap::RamTransferAddress);
    const psxrecomp::Address transferCtrlReg =
        spuRegisterAddress(Spu::RegisterMap::TransferControl);
    const psxrecomp::Address spuDmaBase =
        DmaController::ChannelBase +
        DmaController::ChannelStride * static_cast<psxrecomp::Address>(DmaPort::Spu);

    u32 handle = system.events().openEvent(EventClass::Spu, EventSpec::CommandDone,
                                           EventMode::NoCallback, 0);
    assert(handle != 0xFFFFFFFFu);
    assert(system.events().enableEvent(handle));

    system.writeMmioExplicit<psxrecomp::u16>(transferCtrlReg, 0x0004u);
    system.writeMmioExplicit<psxrecomp::u16>(transferAddrReg, 0x0000u);
    system.writeMmioExplicit<psxrecomp::u16>(controlReg, 0x0000u);
    system.tickCpuCycles(kHandshakeDelayCycles);

    system.write<psxrecomp::u32>(0x00014000u, 0xAABBCCDDu);
    system.writeMmioExplicit<psxrecomp::u32>(spuDmaBase + 0x0, 0x00014000u);
    system.writeMmioExplicit<psxrecomp::u32>(spuDmaBase + 0x4, 0x00000001u);
    system.writeMmioExplicit<psxrecomp::u32>(spuDmaBase + 0x8, 0x01000001u);
    system.tickCpuCycles(2048u);
    assert(!system.events().isEventDelivered(handle));

    system.writeMmioExplicit<psxrecomp::u16>(controlReg, 0x0020u);
    system.tickCpuCycles(kHandshakeDelayCycles);
    system.writeMmioExplicit<psxrecomp::u32>(spuDmaBase + 0x0, 0x00014000u);
    system.writeMmioExplicit<psxrecomp::u32>(spuDmaBase + 0x4, 0x00000001u);
    system.writeMmioExplicit<psxrecomp::u32>(spuDmaBase + 0x8, 0x01000001u);
    assert(!system.events().isEventDelivered(handle));
    system.tickCpuCycles(2048u);
    assert(system.events().isEventDelivered(handle));

    u32 regs[32] = {};
    callWaitEvent(system, handle, regs);
    assert(regs[2] == 1);
    assert(!system.events().isEventDelivered(handle));

    std::cerr << "[PASS] SPU WaitEvent tracks real DMA completion edges\n";
}

// ---------------------------------------------------------------
// Test 9: isEventDelivered is non-mutating.
// ---------------------------------------------------------------
static void testIsEventDeliveredNonMutating()
{
    KernelEventTable table;
    u32 handle = table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
    assert(handle != 0xFFFFFFFFu);
    table.enableEvent(handle);
    table.deliverEvent(handle);

    // Multiple calls to isEventDelivered should not change state.
    assert(table.isEventDelivered(handle));
    assert(table.isEventDelivered(handle));
    assert(table.isEventDelivered(handle));

    // Event should still be Delivered.
    assert(table.getEvent(handle)->status == EventStatus::Delivered);

    std::cerr << "[PASS] isEventDelivered is non-mutating\n";
}

int main()
{
    testAlreadyDelivered();
    testDeliveredByIrq();
    testTestEventResetsDelivery();
    testInvalidHandle();
    testDisabledEvent();
    testCallbackModeNonBlocking();
    testUnDeliverThenWaitBlocks();
    testSpuWaitEventTracksRealDmaCompletion();
    testIsEventDeliveredNonMutating();

    std::cout << "All WaitEvent tests passed.\n";
    return 0;
}
