/**
 * @file irq_service_split_test.cpp
 * @brief Tests for the PSX-SPX "no nested exceptions" rule (PR-RV13).
 *
 * PSX-SPX states the BIOS kernel does not support nested exceptions.
 * The runtime must never call exceptionEnter() while already inside
 * BIOS exception handling (m_inCallbackInvocation == true).
 *
 * Covers:
 *  - Nested exception entry is blocked when already in exception+callback mode
 *  - Normal first IRQ still enters the exception handler once (regression)
 */
#include "psxrecomp/runtime/cop0.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <iostream>

using psxrecomp::u32;
using psxrecomp::runtime::Cop0;
using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::PsxSystem;
namespace EventClass = psxrecomp::runtime::EventClass;
namespace EventSpec = psxrecomp::runtime::EventSpec;
using psxrecomp::runtime::EventMode;

namespace
{

// ---------------------------------------------------------------
// Test A: No fresh nested exceptionEnter when already in exception mode
//
// Set up:
//   - pending VBlank IRQ with mask
//   - COP0 manually put into exception mode (isInExceptionMode=true,
//     shouldTakeInterruptException=false)
//   - m_inCallbackInvocation=true (via invokeCallback)
//
// Verify from inside the callback invoker:
//   - COP0 Status is unchanged after serviceInterrupts (no mode bit shift
//     from a second exceptionEnter)
//   - shouldTakeInterruptException() stays false (no incorrect claim of
//     fresh-exception eligibility)
// ---------------------------------------------------------------
static void testNoFreshExceptionEnterDuringCallback()
{
    PsxSystem system;
    assert(system.initialize());

    // Configure COP0: IEc=1, KUp=0, IM2 enabled (standard interrupt enable).
    system.cop0().mtc0(Cop0::RegisterIndex::Status, 0x040Bu);
    // Put COP0 into exception mode (clears IEc, saves old mode in bits [3:2]).
    // After this: shouldTakeInterruptException() == false, isInExceptionMode() == true.
    system.cop0().exceptionEnter(Cop0::ExceptionCode::Interrupt, 0x80001000u, false);
    assert(system.cop0().isInExceptionMode());
    assert(!system.cop0().shouldTakeInterruptException());

    const u32 statusBeforeCallback = system.cop0().mfc0(Cop0::RegisterIndex::Status);

    // Enable VBlank interrupt mask and raise the IRQ.
    system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
    system.interrupts().raise(InterruptLine::VBlank);

    constexpr u32 outerAddr = 0x80012000u;

    bool callbackRan = false;
    system.setCallbackInvoker(
        [&](u32 address) -> u32
        {
            if (address == outerAddr)
            {
                callbackRan = true;
                // Inside callback: m_inCallbackInvocation=true, COP0 in exception mode.
                // Verify pre-condition: shouldTakeInterruptException must be false.
                assert(!system.cop0().shouldTakeInterruptException());

                const u32 statusBeforeService = system.cop0().mfc0(Cop0::RegisterIndex::Status);

                system.serviceInterrupts();

                // exceptionEnter shifts mode bits left by 2 and would change Status.
                // The in-flight path must NOT call exceptionEnter, so Status is unchanged.
                const u32 statusAfterService = system.cop0().mfc0(Cop0::RegisterIndex::Status);
                assert(statusAfterService == statusBeforeService);
            }
            return 0;
        });

    system.invokeCallback(outerAddr);

    assert(callbackRan);
    // COP0 Status as seen from outside must also be unchanged from the original
    // exception-mode value set before invokeCallback.
    assert(system.cop0().mfc0(Cop0::RegisterIndex::Status) == statusBeforeCallback);

    std::cerr << "[PASS] No fresh exceptionEnter when already in exception+callback mode\n";
}

// ---------------------------------------------------------------
// Test B: Normal first IRQ still enters the exception handler once
//
// PSX-SPX regression: when no exception is currently active and a
// pending IRQ is present, serviceInterrupts() must call exceptionEnter()
// exactly once so the IRQ is dispatched normally.
//
// Set up:
//   - COP0 NOT in exception mode (fresh state, IEc=1, IM2 enabled)
//   - VBlank IRQ raised and masked
//   - Kernel event for VBlank in Callback mode
//
// Verify:
//   - Callback is invoked (exception was entered and IRQ dispatched)
//   - COP0 was in exception mode during the callback (exceptionEnter called)
//   - COP0 returns to normal mode after serviceInterrupts() (rfe called)
// ---------------------------------------------------------------
static void testNormalFirstIrqEntersOnce()
{
    PsxSystem system;
    assert(system.initialize());

    // Starting state: IEc=1, KUp=0, IM2 enabled — not in exception mode.
    system.cop0().mtc0(Cop0::RegisterIndex::Status, 0x040Bu);
    assert(!system.cop0().isInExceptionMode());

    // Enable VBlank mask and raise the IRQ.
    system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
    system.interrupts().raise(InterruptLine::VBlank);

    // Open a VBlank kernel event in Callback mode.
    constexpr u32 vblankCallbackAddr = 0x80015000u;
    const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                 EventMode::Callback, vblankCallbackAddr);
    assert(system.events().enableEvent(handle));

    bool callbackInvoked = false;
    bool inExceptionModeInsideCallback = false;

    system.setCallbackInvoker(
        [&](u32 address) -> u32
        {
            if (address == vblankCallbackAddr)
            {
                callbackInvoked = true;
                // exceptionEnter() must have been called before dispatching the
                // callback, so COP0 must report exception mode here.
                inExceptionModeInsideCallback = system.cop0().isInExceptionMode();
            }
            return 0;
        });

    // serviceInterrupts from outside any callback — fresh exception path.
    system.observeProgramCounter(0x80015004u);
    system.serviceInterrupts();

    // The callback must have fired (exception was entered, IRQ dispatched).
    assert(callbackInvoked);
    // COP0 must have been in exception mode during the callback.
    assert(inExceptionModeInsideCallback);
    // After serviceInterrupts returns, rfe() must have restored normal mode.
    assert(!system.cop0().isInExceptionMode());

    std::cerr << "[PASS] Normal first IRQ enters once: exceptionEnter called, rfe called on exit\n";
}

} // namespace

int main()
{
    testNoFreshExceptionEnterDuringCallback();
    testNormalFirstIrqEntersOnce();

    std::cerr << "\nAll IRQ service split tests passed.\n";
    return 0;
}
