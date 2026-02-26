/**
 * @file interrupt_dispatcher_test.cpp
 * @brief Tests for InterruptDispatcher: mask/critical-section/deferred
 *        behavior, callback ordering, and budget enforcement.
 */
#include "interrupt_dispatcher_test_sections.h"
#include "psxrecomp/runtime/interrupt_dispatcher.h"
#include "psxrecomp/runtime/kernel_events.h"
#include "psxrecomp/runtime/logger.h"

#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    using psxrecomp::u32;
    using psxrecomp::runtime::InterruptController;
    using psxrecomp::runtime::InterruptDispatcher;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::KernelEventTable;
    using psxrecomp::runtime::RuntimeLogger;

    namespace EventClass = psxrecomp::runtime::EventClass;
    namespace EventSpec = psxrecomp::runtime::EventSpec;
    using psxrecomp::runtime::EventMode;
    using psxrecomp::runtime::EventStatus;

    // ---------------------------------------------------------------
    // Test 1: No pending interrupts — no callbacks dispatched
    // ---------------------------------------------------------------
    {
        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();

        std::vector<u32> invoked;
        dispatcher.setCallbackInvoker(
            [&invoked](u32 addr) -> u32
            {
                invoked.push_back(addr);
                return 0;
            });

        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);
        assert(invoked.empty());
        assert(dispatcher.callbacksDispatched() == 0);
        std::cerr << "[PASS] no pending interrupts\n";
    }

    // ---------------------------------------------------------------
    // Test 2: VBlank interrupt dispatches registered callback
    // ---------------------------------------------------------------
    {
        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();

        // Register a VBlank callback event
        u32 handle = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                      0x80010100);
        events.enableEvent(handle);

        // Enable VBlank in mask and raise it
        interrupts.writeMask(static_cast<u32>(InterruptLine::VBlank));
        interrupts.raise(InterruptLine::VBlank);

        std::vector<u32> invoked;
        dispatcher.setCallbackInvoker(
            [&invoked](u32 addr) -> u32
            {
                invoked.push_back(addr);
                return 0;
            });

        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);
        assert(invoked.size() == 1);
        assert(invoked[0] == 0x80010100);
        assert(dispatcher.callbacksDispatched() == 1);
        // IRQ should be acknowledged
        assert((interrupts.readStatus() & static_cast<u32>(InterruptLine::VBlank)) == 0);
        std::cerr << "[PASS] VBlank dispatches callback\n";
    }

    // ---------------------------------------------------------------
    // Test 3: Critical section prevents dispatch
    // ---------------------------------------------------------------
    {
        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();

        u32 handle = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                      0x80010200);
        events.enableEvent(handle);
        interrupts.writeMask(static_cast<u32>(InterruptLine::VBlank));
        interrupts.raise(InterruptLine::VBlank);

        std::vector<u32> invoked;
        dispatcher.setCallbackInvoker(
            [&invoked](u32 addr) -> u32
            {
                invoked.push_back(addr);
                return 0;
            });

        // In critical section
        dispatcher.serviceInterrupts(interrupts, events, 1, &logger);
        assert(invoked.empty());
        // Interrupt should still be pending
        assert((interrupts.readStatus() & static_cast<u32>(InterruptLine::VBlank)) != 0);
        std::cerr << "[PASS] critical section prevents dispatch\n";
    }

    // ---------------------------------------------------------------
    // Test 4: Masked interrupts are not dispatched
    // ---------------------------------------------------------------
    {
        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();

        u32 handle = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                      0x80010300);
        events.enableEvent(handle);
        // Do NOT enable VBlank in mask
        interrupts.writeMask(0);
        interrupts.raise(InterruptLine::VBlank);

        std::vector<u32> invoked;
        dispatcher.setCallbackInvoker(
            [&invoked](u32 addr) -> u32
            {
                invoked.push_back(addr);
                return 0;
            });

        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);
        assert(invoked.empty());
        std::cerr << "[PASS] masked interrupts not dispatched\n";
    }

    // ---------------------------------------------------------------
    // Test 5: Multiple simultaneous IRQs dispatched in priority order
    // ---------------------------------------------------------------
    {
        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();

        // Register VBlank and Timer0 events
        u32 h1 = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                  0x80010400);
        u32 h2 = events.openEvent(EventClass::Timer0, EventSpec::Counter, EventMode::Callback,
                                  0x80010500);
        events.enableEvent(h1);
        events.enableEvent(h2);

        u32 mask =
            static_cast<u32>(InterruptLine::VBlank) | static_cast<u32>(InterruptLine::Timer0);
        interrupts.writeMask(mask);
        interrupts.raise(InterruptLine::VBlank);
        interrupts.raise(InterruptLine::Timer0);

        std::vector<u32> invoked;
        dispatcher.setCallbackInvoker(
            [&invoked](u32 addr) -> u32
            {
                invoked.push_back(addr);
                return 0;
            });

        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);
        // VBlank has higher priority (processed first)
        assert(invoked.size() == 2);
        assert(invoked[0] == 0x80010400); // VBlank
        assert(invoked[1] == 0x80010500); // Timer0
        std::cerr << "[PASS] multiple IRQs in priority order\n";
    }

    // ---------------------------------------------------------------
    // Test 6: Budget limits callbacks per service call
    // ---------------------------------------------------------------
    {
        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();
        dispatcher.setMaxCallbacksPerService(1);

        u32 h1 = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                  0x80010600);
        u32 h2 = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                  0x80010700);
        events.enableEvent(h1);
        events.enableEvent(h2);

        interrupts.writeMask(static_cast<u32>(InterruptLine::VBlank));
        interrupts.raise(InterruptLine::VBlank);

        std::vector<u32> invoked;
        dispatcher.setCallbackInvoker(
            [&invoked](u32 addr) -> u32
            {
                invoked.push_back(addr);
                return 0;
            });

        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);
        // Budget is 1 — should invoke first callback immediately and defer second
        // The deferred callback is drained at the end of serviceInterrupts
        assert(invoked.size() == 2);
        assert(invoked[0] == 0x80010600);
        assert(invoked[1] == 0x80010700);
        std::cerr << "[PASS] budget limits then drains deferred\n";
    }

    // ---------------------------------------------------------------
    // Test 7: NoCallback events are delivered but no invoker called
    // ---------------------------------------------------------------
    {
        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();

        u32 handle =
            events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
        events.enableEvent(handle);
        interrupts.writeMask(static_cast<u32>(InterruptLine::VBlank));
        interrupts.raise(InterruptLine::VBlank);

        std::vector<u32> invoked;
        dispatcher.setCallbackInvoker(
            [&invoked](u32 addr) -> u32
            {
                invoked.push_back(addr);
                return 0;
            });

        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);
        assert(invoked.empty());
        // Event should be Delivered for polling
        assert(events.getEvent(handle)->status == EventStatus::Delivered);
        std::cerr << "[PASS] NoCallback events delivered without invoker\n";
    }

    // ---------------------------------------------------------------
    // Test 8: Reset clears dispatcher state
    // ---------------------------------------------------------------
    {
        InterruptDispatcher dispatcher;
        dispatcher.reset();
        assert(dispatcher.callbacksDispatched() == 0);
        std::cerr << "[PASS] reset clears dispatcher state\n";
    }

    // ---------------------------------------------------------------
    // Test 9: No invoker installed — callbacks delivered but not invoked
    // ---------------------------------------------------------------
    {
        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();
        // No invoker set

        u32 handle = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                      0x80010800);
        events.enableEvent(handle);
        interrupts.writeMask(static_cast<u32>(InterruptLine::VBlank));
        interrupts.raise(InterruptLine::VBlank);

        // Should not crash — gracefully skip invocation
        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);
        assert(dispatcher.callbacksDispatched() == 0);
        // Callback events remain enabled for future IRQ edges.
        assert(events.getEvent(handle)->status == EventStatus::Enabled);
        std::cerr << "[PASS] no invoker — silent skip\n";
    }

    // ---------------------------------------------------------------
    // Test 10: Callback events dispatch on every IRQ edge
    // ---------------------------------------------------------------
    {
        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();

        u32 handle = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                      0x80010880);
        events.enableEvent(handle);
        interrupts.writeMask(static_cast<u32>(InterruptLine::VBlank));

        std::vector<u32> invoked;
        dispatcher.setCallbackInvoker(
            [&invoked](u32 addr) -> u32
            {
                invoked.push_back(addr);
                return 0;
            });

        interrupts.raise(InterruptLine::VBlank);
        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);
        interrupts.raise(InterruptLine::VBlank);
        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);

        assert(invoked.size() == 2);
        assert(invoked[0] == 0x80010880);
        assert(invoked[1] == 0x80010880);
        std::cerr << "[PASS] callback event dispatches repeatedly\n";
    }

    // ---------------------------------------------------------------
    // Test 11: Interrupted spec used for general IRQ delivery
    // ---------------------------------------------------------------
    {
        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();

        u32 handle = events.openEvent(EventClass::Cdrom, EventSpec::Interrupted,
                                      EventMode::Callback, 0x80010900);
        events.enableEvent(handle);
        interrupts.writeMask(static_cast<u32>(InterruptLine::Cdrom));
        interrupts.raise(InterruptLine::Cdrom);

        std::vector<u32> invoked;
        dispatcher.setCallbackInvoker(
            [&invoked](u32 addr) -> u32
            {
                invoked.push_back(addr);
                return 0;
            });

        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);
        assert(invoked.size() == 1);
        assert(invoked[0] == 0x80010900);
        std::cerr << "[PASS] Interrupted spec for Cdrom IRQ\n";
    }

    runInterruptDispatcherExceptionTests();

    std::cerr << "All interrupt_dispatcher tests passed.\n";
    return 0;
}
