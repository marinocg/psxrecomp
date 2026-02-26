#include "interrupt_dispatcher_test_sections.h"

#include "psxrecomp/runtime/interrupt_dispatcher.h"
#include "psxrecomp/runtime/kernel_events.h"
#include "psxrecomp/runtime/logger.h"

#include <cassert>
#include <iostream>
#include <vector>

void runInterruptDispatcherExceptionTests()
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

    {
        struct TestReturnFromException
        {
        };

        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();

        constexpr u32 callback1 = 0x80010A00;
        constexpr u32 callback2 = 0x80010A10;
        u32 h1 = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                  callback1);
        u32 h2 = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                  callback2);
        events.enableEvent(h1);
        events.enableEvent(h2);

        interrupts.writeMask(static_cast<u32>(InterruptLine::VBlank));
        interrupts.raise(InterruptLine::VBlank);

        std::vector<u32> invoked;
        dispatcher.setCallbackInvoker(
            [&invoked, callback1](u32 addr) -> u32
            {
                invoked.push_back(addr);
                if (addr == callback1)
                {
                    throw TestReturnFromException{};
                }
                return 0;
            });

        bool threw = false;
        try
        {
            dispatcher.serviceInterrupts(interrupts, events, 0, &logger);
        }
        catch (const TestReturnFromException&)
        {
            threw = true;
        }
        assert(threw);
        assert(invoked.size() == 1);
        assert(invoked[0] == callback1);

        dispatcher.setCallbackInvoker(
            [&invoked](u32 addr) -> u32
            {
                invoked.push_back(addr);
                return 0;
            });
        interrupts.raise(InterruptLine::VBlank);
        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);

        assert(invoked.size() == 3);
        assert(invoked[1] == callback1);
        assert(invoked[2] == callback2);
        std::cerr << "[PASS] callback exception short-circuits and recovers\n";
    }

    {
        struct TestReturnFromException
        {
        };

        InterruptController interrupts;
        KernelEventTable events;
        InterruptDispatcher dispatcher;
        RuntimeLogger logger;

        interrupts.reset();
        events.reset();
        dispatcher.reset();
        dispatcher.setMaxCallbacksPerService(1);

        constexpr u32 callback1 = 0x80010B00;
        constexpr u32 callback2 = 0x80010B10;
        u32 h1 = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                  callback1);
        u32 h2 = events.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                  callback2);
        events.enableEvent(h1);
        events.enableEvent(h2);

        interrupts.writeMask(static_cast<u32>(InterruptLine::VBlank));
        interrupts.raise(InterruptLine::VBlank);

        std::vector<u32> invoked;
        dispatcher.setCallbackInvoker(
            [&invoked, callback2](u32 addr) -> u32
            {
                invoked.push_back(addr);
                if (addr == callback2)
                {
                    throw TestReturnFromException{};
                }
                return 0;
            });

        bool threw = false;
        try
        {
            dispatcher.serviceInterrupts(interrupts, events, 0, &logger);
        }
        catch (const TestReturnFromException&)
        {
            threw = true;
        }
        assert(threw);
        assert(invoked.size() == 2);
        assert(invoked[0] == callback1);
        assert(invoked[1] == callback2);

        dispatcher.setCallbackInvoker(
            [&invoked](u32 addr) -> u32
            {
                invoked.push_back(addr);
                return 0;
            });
        dispatcher.serviceInterrupts(interrupts, events, 0, &logger);

        assert(invoked.size() == 3);
        assert(invoked[2] == callback2);
        std::cerr << "[PASS] deferred flush exception preserves remaining callbacks\n";
    }
}
