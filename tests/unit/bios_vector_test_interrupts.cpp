#include "bios_vector_test_sections.h"

#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

void runBiosVectorInterruptChainTests()
{
    using psxrecomp::u32;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::LogLevel;
    using psxrecomp::runtime::PsxSystem;
    namespace EventClass = psxrecomp::runtime::EventClass;
    namespace EventSpec = psxrecomp::runtime::EventSpec;
    using psxrecomp::runtime::EventMode;
    // ---------------------------------------------------------------
    // Test 21: NULL regs pointer
    //
    // Should log a warning without crashing.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        bool warningLogged = false;
        system.logger().setMinLevel(LogLevel::Warn);
        system.logger().setCallback(
            [&warningLogged](const psxrecomp::runtime::LogEvent& event)
            {
                if (event.level == LogLevel::Warn)
                {
                    warningLogged = true;
                }
            });

        system.callBiosVector(0xA0, nullptr, 0);
        assert(warningLogged);

        std::cerr << "[PASS] null regs pointer handled\n";
    }

    // ---------------------------------------------------------------
    // Test 22: serviceInterrupts order is chain func1->func2->events
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        constexpr u32 func1 = 0x80016000;
        constexpr u32 func2 = 0x80016010;
        constexpr u32 eventCallback = 0x80016020;
        constexpr u32 node = 0x80017000;

        std::vector<u32> order;
        system.setCallbackInvoker(
            [&order, func1](u32 address) -> u32
            {
                order.push_back(address);
                if (address == func1)
                {
                    return 1;
                }
                return 0;
            });

        system.write<u32>(node + 0x00, 0);
        system.write<u32>(node + 0x04, func2);
        system.write<u32>(node + 0x08, func1);
        system.write<u32>(node + 0x0C, 0);

        u32 regs[32] = {};
        regs[9] = 0x02; // SysEnqIntRP
        regs[4] = 0;    // priority
        regs[5] = node;
        system.callBiosVector(0xC0, regs, 32);
        assert(regs[2] == 1);

        const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                     EventMode::Callback, eventCallback);
        system.events().enableEvent(handle);

        system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
        system.interrupts().raise(InterruptLine::VBlank);
        system.serviceInterrupts();

        assert(order.size() == 3);
        assert(order[0] == func1);
        assert(order[1] == func2);
        assert(order[2] == eventCallback);
        std::cerr << "[PASS] serviceInterrupts chain->event order\n";
    }

    // ---------------------------------------------------------------
    // Test 23: chain func2 is skipped when func1 returns 0
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        constexpr u32 func1 = 0x80016100;
        constexpr u32 func2 = 0x80016110;
        constexpr u32 eventCallback = 0x80016120;
        constexpr u32 node = 0x80017100;

        std::vector<u32> order;
        system.setCallbackInvoker(
            [&order](u32 address) -> u32
            {
                order.push_back(address);
                return 0;
            });

        system.write<u32>(node + 0x00, 0);
        system.write<u32>(node + 0x04, func2);
        system.write<u32>(node + 0x08, func1);
        system.write<u32>(node + 0x0C, 0);

        u32 regs[32] = {};
        regs[9] = 0x02; // SysEnqIntRP
        regs[4] = 0;    // priority
        regs[5] = node;
        system.callBiosVector(0xC0, regs, 32);
        assert(regs[2] == 1);

        const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                     EventMode::Callback, eventCallback);
        system.events().enableEvent(handle);

        system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
        system.interrupts().raise(InterruptLine::VBlank);
        system.serviceInterrupts();

        assert(order.size() == 2);
        assert(order[0] == func1);
        assert(order[1] == eventCallback);
        std::cerr << "[PASS] serviceInterrupts skips func2 on zero v0\n";
    }

    // ---------------------------------------------------------------
    // Test 24: ReturnFromException in chain still preserves kernel-event delivery
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        constexpr u32 func1 = 0x80016200;
        constexpr u32 func2 = 0x80016210;
        constexpr u32 eventCallback = 0x80016220;
        constexpr u32 node = 0x80017200;

        std::vector<u32> order;
        system.setCallbackInvoker(
            [&system, &order, func1](u32 address) -> u32
            {
                order.push_back(address);
                if (address == func1)
                {
                    u32 regs[32] = {};
                    regs[9] = 0x17; // ReturnFromException
                    system.callBiosVector(0xB0, regs, 32);
                }
                return 0;
            });

        system.write<u32>(node + 0x00, 0);
        system.write<u32>(node + 0x04, func2);
        system.write<u32>(node + 0x08, func1);
        system.write<u32>(node + 0x0C, 0);

        u32 regs[32] = {};
        regs[9] = 0x02; // SysEnqIntRP
        regs[4] = 0;    // priority
        regs[5] = node;
        system.callBiosVector(0xC0, regs, 32);
        assert(regs[2] == 1);

        const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                     EventMode::Callback, eventCallback);
        system.events().enableEvent(handle);

        system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
        system.interrupts().raise(InterruptLine::VBlank);
        system.serviceInterrupts();

        assert(order.size() == 2);
        assert(order[0] == func1);
        assert(order[1] == eventCallback);
        assert((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::VBlank)) == 0u);
        std::cerr << "[PASS] chain ReturnFromException preserves event delivery\n";
    }

    // ---------------------------------------------------------------
    // Test 25: HookEntryInt runs before chain/event dispatch and preserves events
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        constexpr u32 chainFunc = 0x80016280;
        constexpr u32 hookDescriptor = 0x80017300;
        constexpr u32 hookCallback = 0x80016300;
        constexpr u32 eventCallback = 0x80016310;
        constexpr u32 node = 0x80017280;

        u32 regs[32] = {};
        regs[9] = 0x13; // setjmp
        regs[4] = hookDescriptor;
        regs[31] = hookCallback; // saved RA / resume target
        regs[29] = 0x80017400;
        regs[30] = 0x80017420;
        system.callBiosVector(0xA0, regs, 32);

        std::fill(std::begin(regs), std::end(regs), 0u);
        regs[9] = 0x19; // HookEntryInt
        regs[4] = hookDescriptor;
        system.callBiosVector(0xB0, regs, 32);

        system.write<u32>(node + 0x00, 0);
        system.write<u32>(node + 0x04, 0);
        system.write<u32>(node + 0x08, chainFunc);
        system.write<u32>(node + 0x0C, 0);

        regs[9] = 0x02; // SysEnqIntRP
        regs[4] = 0;    // priority
        regs[5] = node;
        system.callBiosVector(0xC0, regs, 32);
        assert(regs[2] == 1);

        const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                     EventMode::Callback, eventCallback);
        system.events().enableEvent(handle);

        std::vector<u32> order;
        system.setCallbackInvoker(
            [&system, &order, hookCallback, chainFunc](u32 address) -> u32
            {
                order.push_back(address);
                if (address == hookCallback)
                {
                    u32 regs[32] = {};
                    regs[9] = 0x17; // ReturnFromException
                    system.callBiosVector(0xB0, regs, 32);
                }
                return 0;
            });

        system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
        system.interrupts().raise(InterruptLine::VBlank);
        system.serviceInterrupts();

        assert(order.size() == 2);
        assert(order[0] == hookCallback);
        assert(order[1] == eventCallback);
        assert((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::VBlank)) == 0u);
        std::cerr << "[PASS] HookEntryInt ReturnFromException preserves event delivery\n";
    }

    // Test 26: _96_init maps CDROM INT3 to CommandAck before HookEntryInt
    {
        using psxrecomp::runtime::EventMode;
        namespace EventClass = psxrecomp::runtime::EventClass;
        namespace EventSpec = psxrecomp::runtime::EventSpec;

        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        regs[9] = 0x71;
        regs[4] = 0x00012100u;
        system.callBiosVector(0xA0, regs, 32);

        const u32 handle = system.events().openEvent(EventClass::Cdrom, EventSpec::CommandDone,
                                                     EventMode::Callback, 0x80014000u);
        assert(handle != 0xFFFFFFFFu);
        assert(system.events().enableEvent(handle));

        bool callbackInvoked = false;
        system.setCallbackInvoker(
            [&system, &callbackInvoked](u32 address) -> u32
            {
                if (address == 0x80014000u)
                {
                    callbackInvoked = true;
                    system.cdrom().writeInterruptFlags(0x07u);
                }
                return 0;
            });

        system.interrupts().writeMask(system.interrupts().readMask() |
                                      static_cast<u32>(InterruptLine::Cdrom));
        system.cdrom().writeCommand(0x01); // Getstat -> INT3 / CommandAck
        assert((system.cdrom().readInterruptFlags() & 0x07u) == 0x03u);

        system.serviceInterrupts();

        assert(callbackInvoked);
        assert((system.cdrom().readInterruptFlags() & 0x07u) == 0u);
        assert((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::Cdrom)) == 0u);
        assert(system.cdrom().readResponse() == 0x00u);

        std::cerr << "[PASS] _96_init routes CDROM INT3 to completion event\n";
    }

    // Test 27: HookEntryInt acknowledging an IRQ does not starve kernel events.
    {
        using psxrecomp::runtime::EventMode;

        PsxSystem system;
        assert(system.initialize());

        constexpr u32 hookDescriptor = 0x80017500;
        constexpr u32 hookCallback = 0x80016400;
        constexpr u32 eventCallback = 0x80016410;

        u32 regs[32] = {};
        regs[9] = 0x13; // setjmp
        regs[4] = hookDescriptor;
        regs[31] = hookCallback;
        regs[29] = 0x80017600;
        regs[30] = 0x80017620;
        system.callBiosVector(0xA0, regs, 32);

        std::fill(std::begin(regs), std::end(regs), 0u);
        regs[9] = 0x19; // HookEntryInt
        regs[4] = hookDescriptor;
        system.callBiosVector(0xB0, regs, 32);

        const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                     EventMode::Callback, eventCallback);
        system.events().enableEvent(handle);

        std::vector<u32> order;
        system.setCallbackInvoker(
            [&system, &order, hookCallback](u32 address) -> u32
            {
                order.push_back(address);
                if (address == hookCallback)
                {
                    system.interrupts().writeStatus(~static_cast<u32>(InterruptLine::VBlank));
                }
                return 0;
            });

        system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
        system.interrupts().raise(InterruptLine::VBlank);
        system.serviceInterrupts();

        assert(order.size() == 2);
        assert(order[0] == hookCallback);
        assert(order[1] == eventCallback);
        assert((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::VBlank)) == 0u);
        std::cerr << "[PASS] HookEntryInt ack does not starve kernel events\n";
    }
}
