/**
 * @file irq_service_split_test.cpp
 * @brief Tests for the fresh-vs-in-flight IRQ service split (PR-RV13).
 *
 * Covers:
 *  - No fresh nested exceptionEnter when already in exception+callback mode
 *  - In-flight service path delivers kernel events when callback is active
 *  - Deadlock state (pending + in_callback + exception_mode) no longer blocks
 */
#include "psxrecomp/runtime/cop0.h"
#include "psxrecomp/runtime/psx_system.h"

#include <array>
#include <cassert>
#include <iostream>

using psxrecomp::u32;
using psxrecomp::runtime::Cop0;
using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::LogLevel;
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

                const u32 statusBeforeService =
                    system.cop0().mfc0(Cop0::RegisterIndex::Status);

                system.serviceInterrupts();

                // exceptionEnter shifts mode bits left by 2 and would change Status.
                // The in-flight path must NOT call exceptionEnter, so Status is unchanged.
                const u32 statusAfterService =
                    system.cop0().mfc0(Cop0::RegisterIndex::Status);
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
// Test B: In-flight service delivers kernel events while inside callback
//
// Set up:
//   - COP0 in exception mode (isInExceptionMode=true)
//   - VBlank IRQ raised and masked
//   - Kernel event for VBlank with a Callback pointing to vblankEventAddr
//   - Callback invoker: outer callback calls serviceInterrupts(); inner
//     callback (vblankEventAddr) records that it was called
//
// Verify:
//   - vblankEventCallback is invoked from the in-flight path (even though
//     m_inCallbackInvocation=true when serviceInterrupts is called)
//   - the old "pending + in_callback + exception_mode = do nothing" path
//     is no longer taken
// ---------------------------------------------------------------
static void testInFlightDeliversKernelEventFromCallback()
{
    PsxSystem system;
    assert(system.initialize());

    // Put COP0 into exception mode so irqTakeEligible=false.
    system.cop0().mtc0(Cop0::RegisterIndex::Status, 0x040Bu);
    system.cop0().exceptionEnter(Cop0::ExceptionCode::Interrupt, 0x80001000u, false);
    assert(system.cop0().isInExceptionMode());

    // Raise VBlank with the mask enabled.
    system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
    system.interrupts().raise(InterruptLine::VBlank);

    // Open a VBlank kernel event in Callback mode.
    constexpr u32 vblankEventAddr = 0x80014000u;
    const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                  EventMode::Callback, vblankEventAddr);
    assert(system.events().enableEvent(handle));

    constexpr u32 outerAddr = 0x80012000u;

    bool vblankEventInvoked = false;
    system.setCallbackInvoker(
        [&](u32 address) -> u32
        {
            if (address == outerAddr)
            {
                // m_inCallbackInvocation=true, COP0 in exception mode.
                // serviceInterrupts must use the in-flight path and deliver the event.
                system.serviceInterrupts();
            }
            if (address == vblankEventAddr)
            {
                vblankEventInvoked = true;
            }
            return 0;
        });

    system.invokeCallback(outerAddr);

    assert(vblankEventInvoked);
    std::cerr << "[PASS] In-flight path delivers kernel event from inside callback\n";
}

// ---------------------------------------------------------------
// Test C: Deadlock state no longer triggers irq_blocked_diagnostic
//
// The specific deadlock state is:
//   pending IRQ + m_inCallbackInvocation=true + COP0 in exception mode
//
// Before the fix, this produced "pending forever, do nothing" and
// eventually logged irq_blocked_diagnostic (Error level).  After the fix,
// the in-flight path makes progress and the counter never reaches 64.
//
// Verify by installing a logger callback and asserting no Error-level
// irq_blocked_diagnostic is logged across 128 serviceInterrupts() calls
// from within a callback while in exception mode.
// ---------------------------------------------------------------
static void testDeadlockStateNoLongerLogsBlockedDiagnostic()
{
    PsxSystem system;
    assert(system.initialize());

    system.cop0().mtc0(Cop0::RegisterIndex::Status, 0x040Bu);
    system.cop0().exceptionEnter(Cop0::ExceptionCode::Interrupt, 0x80001000u, false);

    system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
    system.interrupts().raise(InterruptLine::VBlank);

    constexpr u32 vblankEventAddr = 0x80014100u;
    const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                  EventMode::Callback, vblankEventAddr);
    assert(system.events().enableEvent(handle));

    bool blockedDiagLogged = false;
    system.logger().setCallback(
        [&](const psxrecomp::runtime::LogEvent& event)
        {
            if (event.level == LogLevel::Error &&
                event.message.find("irq_blocked_diagnostic") != std::string::npos)
            {
                blockedDiagLogged = true;
            }
        });
    system.logger().setMinLevel(LogLevel::Error);

    constexpr u32 outerAddr = 0x80012100u;
    int serviceCallsFromCallback = 0;

    system.setCallbackInvoker(
        [&](u32 address) -> u32
        {
            if (address == outerAddr)
            {
                // Call serviceInterrupts 128 times from inside the callback
                // while COP0 is in exception mode.  The in-flight path should
                // service the VBlank and drain it within the first few calls.
                for (int i = 0; i < 128; ++i)
                {
                    system.serviceInterrupts();
                    ++serviceCallsFromCallback;
                }
            }
            // Accept the vblankEventAddr callback silently.
            return 0;
        });

    system.invokeCallback(outerAddr);

    assert(serviceCallsFromCallback == 128);
    assert(!blockedDiagLogged);

    std::cerr << "[PASS] Deadlock state (pending+callback+exception) no longer logs "
                 "irq_blocked_diagnostic\n";
}

} // namespace

int main()
{
    testNoFreshExceptionEnterDuringCallback();
    testInFlightDeliversKernelEventFromCallback();
    testDeadlockStateNoLongerLogsBlockedDiagnostic();

    std::cerr << "\nAll IRQ service split tests passed.\n";
    return 0;
}
