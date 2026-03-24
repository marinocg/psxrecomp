/**
 * @file dispatcher_no_callback_irq_test.cpp
 * @brief Regression tests for dispatcher no-callback IRQ acknowledgment.
 *
 * A kernel event registered in NoCallback (polling) mode must still cause
 * the hardware IRQ line to be acknowledged (I_STAT bit cleared) when the
 * event is delivered, even though no callback address is invoked.
 *
 * Previously the dispatcher only acknowledged the line if the callback
 * vector was non-empty; NoCallback events contributed to deliveredCount
 * but were excluded from the callback list, so the IRQ bit was never acked.
 */
#include "psxrecomp/runtime/interrupt_controller.h"
#include "psxrecomp/runtime/interrupt_dispatcher.h"
#include "psxrecomp/runtime/kernel_events.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <iostream>

using psxrecomp::u32;
using psxrecomp::runtime::EventMode;
namespace EventSpec = psxrecomp::runtime::EventSpec;
using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::KernelEventTable;
using psxrecomp::runtime::PsxSystem;
namespace EventClass = psxrecomp::runtime::EventClass;

namespace
{

// ---------------------------------------------------------------
// Test 1: NoCallback VBlank event causes I_STAT VBlank bit to be acked
// ---------------------------------------------------------------
static void testNoCallbackEventAcksIrqLine()
{
    PsxSystem system;
    assert(system.initialize());

    // Open a VBlank event in NoCallback (polling) mode, no callback address.
    const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Interrupted,
                                                  EventMode::NoCallback, 0u);
    assert(handle != 0xFFFFFFFFu);
    system.events().enableEvent(handle);

    system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
    system.interrupts().raise(InterruptLine::VBlank);
    assert((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::VBlank)) != 0u);

    // Service interrupts — the dispatcher must deliver the NoCallback event
    // and acknowledge I_STAT bit 0 (VBlank).
    system.observeProgramCounter(0x80010000u);
    system.serviceInterrupts();

    // I_STAT VBlank bit must be cleared by the dispatcher ack.
    assert((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::VBlank)) == 0u);

    // The event must now be in Delivered state (NoCallback semantics).
    assert(system.events().isEventDelivered(handle));

    std::cerr << "[PASS] NoCallback VBlank event acks I_STAT VBlank bit\n";
}

// ---------------------------------------------------------------
// Test 2: NoCallback event with testEvent resets to Enabled
// ---------------------------------------------------------------
static void testNoCallbackTestEventResetsToEnabled()
{
    PsxSystem system;
    assert(system.initialize());

    const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Interrupted,
                                                  EventMode::NoCallback, 0u);
    system.events().enableEvent(handle);

    system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
    system.interrupts().raise(InterruptLine::VBlank);

    system.observeProgramCounter(0x80010004u);
    system.serviceInterrupts();

    // Event delivered → testEvent returns 1 and resets to Enabled.
    assert(system.events().isEventDelivered(handle));
    assert(system.events().testEvent(handle) == 1u);
    assert(!system.events().isEventDelivered(handle));

    std::cerr << "[PASS] NoCallback testEvent returns 1 and resets to Enabled\n";
}

// ---------------------------------------------------------------
// Test 3: Multiple lines — NoCallback on VBlank acks VBlank; Timer0 IRQ
//         (no event registered) must NOT be acked by the dispatcher.
// ---------------------------------------------------------------
static void testNoCallbackOnlyAcksRegisteredLine()
{
    PsxSystem system;
    assert(system.initialize());

    const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Interrupted,
                                                  EventMode::NoCallback, 0u);
    system.events().enableEvent(handle);

    constexpr u32 vblankBit = static_cast<u32>(InterruptLine::VBlank);
    constexpr u32 timer0Bit = static_cast<u32>(InterruptLine::Timer0);
    system.interrupts().writeMask(vblankBit | timer0Bit);
    system.interrupts().raise(InterruptLine::VBlank);
    system.interrupts().raise(InterruptLine::Timer0);

    system.observeProgramCounter(0x80010008u);
    system.serviceInterrupts();

    // VBlank was acked (NoCallback event delivered it).
    assert((system.interrupts().readStatus() & vblankBit) == 0u);

    // Timer0 has no registered event → dispatcher does NOT ack it.
    assert((system.interrupts().readStatus() & timer0Bit) != 0u);

    std::cerr << "[PASS] NoCallback only acks registered line; unregistered lines remain\n";
}

// ---------------------------------------------------------------
// Test 4: Callback event also acks the IRQ line
//
// Regression guard: a Callback event with a valid address must still ack
// I_STAT (existing behaviour that must not regress).
// ---------------------------------------------------------------
static void testCallbackEventAcksIrqLine()
{
    PsxSystem system;
    assert(system.initialize());

    constexpr u32 callbackAddr = 0x80016000u;
    bool callbackInvoked = false;
    system.setCallbackInvoker([&](u32 addr) -> u32
    {
        if (addr == callbackAddr)
        {
            callbackInvoked = true;
        }
        return 0u;
    });

    const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Interrupted,
                                                  EventMode::Callback, callbackAddr);
    system.events().enableEvent(handle);

    system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
    system.interrupts().raise(InterruptLine::VBlank);

    system.observeProgramCounter(0x8001000Cu);
    system.serviceInterrupts();

    assert(callbackInvoked);
    assert((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::VBlank)) == 0u);

    std::cerr << "[PASS] Callback event invokes callback and acks I_STAT\n";
}

} // namespace

int main()
{
    testNoCallbackEventAcksIrqLine();
    testNoCallbackTestEventResetsToEnabled();
    testNoCallbackOnlyAcksRegisteredLine();
    testCallbackEventAcksIrqLine();

    std::cerr << "\nAll dispatcher no-callback IRQ tests passed.\n";
    return 0;
}
