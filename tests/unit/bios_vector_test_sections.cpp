#include "bios_vector_test_sections.h"

#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <cassert>
#include <iostream>

void runBiosVectorKernelEventTests()
{
    using psxrecomp::Address;
    using psxrecomp::u32;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::LogLevel;
    using psxrecomp::runtime::PsxSystem;
    namespace EventClass = psxrecomp::runtime::EventClass;
    namespace EventSpec = psxrecomp::runtime::EventSpec;
    using psxrecomp::runtime::EventMode;
    // ---------------------------------------------------------------
    // Test 13: B0 vector - OpenEvent (function 0x08)
    //
    // Should return a valid event handle from the kernel event table.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        regs[9] = 0x08;       // OpenEvent
        regs[4] = 0xF0000001; // class = VBlank
        regs[5] = 0x0001;     // spec = Counter
        regs[6] = 0x2000;     // mode = NoCallback
        regs[7] = 0;          // callback = none
        system.callBiosVector(0xB0, regs, 32);
        // Should return a real handle, not the old fake 0x10
        assert(regs[2] != 0xFFFFFFFFu);
        assert((regs[2] & 0xFF000000u) == 0xF1000000u);

        std::cerr << "[PASS] B0 OpenEvent (real handle)\n";
    }

    // ---------------------------------------------------------------
    // Test 14: B0 vector - TestEvent (function 0x0B)
    //
    // Should return 0 for enabled-but-undelivered, 1 after delivery.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        // Open an event
        u32 regs[32] = {};
        regs[9] = 0x08;       // OpenEvent
        regs[4] = 0xF0000001; // class = VBlank
        regs[5] = 0x0001;     // spec = Counter
        regs[6] = 0x2000;     // mode = NoCallback
        regs[7] = 0;
        system.callBiosVector(0xB0, regs, 32);
        u32 handle = regs[2];

        // Enable it
        regs[9] = 0x0C; // EnableEvent
        regs[4] = handle;
        system.callBiosVector(0xB0, regs, 32);

        // Test — should be 0 (not yet delivered)
        regs[9] = 0x0B; // TestEvent
        regs[4] = handle;
        system.callBiosVector(0xB0, regs, 32);
        assert(regs[2] == 0);

        // Deliver it
        regs[9] = 0x07;       // DeliverEvent
        regs[4] = 0xF0000001; // class = VBlank
        regs[5] = 0x0001;     // spec = Counter
        system.callBiosVector(0xB0, regs, 32);

        // Test — should be 1
        regs[9] = 0x0B; // TestEvent
        regs[4] = handle;
        system.callBiosVector(0xB0, regs, 32);
        assert(regs[2] == 1);

        std::cerr << "[PASS] B0 TestEvent (real state transitions)\n";
    }

    // ---------------------------------------------------------------
    // Test 15: B0 vector - GetC0Table/GetB0Table expose writable BIOS tables
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        regs[9] = 0x56; // GetC0Table
        system.callBiosVector(0xB0, regs, 32);
        const u32 c0Table = regs[2];
        assert(c0Table != 0u);
        assert(system.read<u32>(c0Table + 24u) != 0u);

        regs[9] = 0x57; // GetB0Table
        system.callBiosVector(0xB0, regs, 32);
        const u32 b0Table = regs[2];
        assert(b0Table != 0u);
        assert(system.read<u32>(b0Table + 24u) != 0u);

        std::cerr << "[PASS] B0 GetC0Table/GetB0Table expose kernel tables\n";
    }

    // ---------------------------------------------------------------
    // Test 16: B0 vector stubs (InitPad, StartPad, etc.)
    //
    // Should not crash.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        const u32 stubFunctions[] = {0x07, 0x09, 0x0A, 0x0C, 0x0D, 0x12, 0x13, 0x17,
                                     0x18, 0x19, 0x20, 0x46, 0x4A, 0x4B, 0x5B};
        for (u32 func : stubFunctions)
        {
            regs[9] = func;
            system.callBiosVector(0xB0, regs, 32);
        }

        std::cerr << "[PASS] B0 stub functions (no crash)\n";
    }

    // ---------------------------------------------------------------
    // Test 17: B0 HookEntryInt descriptor callback runs on IRQ service
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 lastCallback = 0;
        system.setCallbackInvoker(
            [&lastCallback](u32 address) -> u32
            {
                lastCallback = address;
                return 0;
            });

        constexpr u32 descriptorAddress = 0x80014000;
        constexpr u32 callbackAddress = 0x80012340;
        u32 regs[32] = {};
        regs[9] = 0x13;              // setjmp
        regs[4] = descriptorAddress; // jmp_buf
        regs[31] = callbackAddress;  // saved RA / resume target
        regs[29] = 0x80018000;       // SP
        regs[30] = 0x80018020;       // FP
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 0);

        std::fill(std::begin(regs), std::end(regs), 0u);
        regs[9] = 0x19;              // HookEntryInt
        regs[4] = descriptorAddress; // descriptor address
        system.callBiosVector(0xB0, regs, 32);
        assert(regs[2] == 0);

        system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
        system.interrupts().raise(InterruptLine::VBlank);
        system.serviceInterrupts();
        assert(lastCallback == callbackAddress);

        regs[9] = 0x18; // ResetEntryInt
        system.callBiosVector(0xB0, regs, 32);
        assert(regs[2] == descriptorAddress);
        lastCallback = 0;
        system.interrupts().raise(InterruptLine::VBlank);
        system.serviceInterrupts();
        assert(lastCallback == 0);

        std::cerr << "[PASS] B0 HookEntryInt descriptor dispatches on IRQ service\n";
    }

    // ---------------------------------------------------------------
    // Test 17a: B0 HookEntryInt ignores invalid longjmp resume addresses
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 lastCallback = 0;
        bool warningLogged = false;
        system.logger().setMinLevel(LogLevel::Warn);
        system.logger().setCallback(
            [&warningLogged](const psxrecomp::runtime::LogEvent& event)
            {
                if (event.level == LogLevel::Warn && event.category == "bios" &&
                    event.message.find("Ignoring HookEntryInt resume address") != std::string::npos)
                {
                    warningLogged = true;
                }
            });
        system.setCallbackInvoker(
            [&lastCallback](u32 address) -> u32
            {
                lastCallback = address;
                return 0;
            });

        constexpr u32 descriptorAddress = 0x80014100;
        constexpr u32 invalidResumeAddress = 0x80012341;
        u32 regs[32] = {};
        regs[9] = 0x13;              // setjmp
        regs[4] = descriptorAddress; // jmp_buf
        regs[31] = invalidResumeAddress;
        regs[29] = 0x80018000;
        regs[30] = 0x80018020;
        system.callBiosVector(0xA0, regs, 32);

        std::fill(std::begin(regs), std::end(regs), 0u);
        regs[9] = 0x19;              // HookEntryInt
        regs[4] = descriptorAddress; // descriptor address
        system.callBiosVector(0xB0, regs, 32);

        system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
        system.interrupts().raise(InterruptLine::VBlank);
        system.serviceInterrupts();

        assert(lastCallback == 0);
        assert(warningLogged);

        std::cerr << "[PASS] B0 HookEntryInt ignores invalid resume addresses\n";
    }

    // ---------------------------------------------------------------
    // Test 17: B0 HookEntryInt no longer treats raw callback as descriptor
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 lastCallback = 0;
        system.setCallbackInvoker(
            [&lastCallback](u32 address) -> u32
            {
                lastCallback = address;
                return 0;
            });

        constexpr u32 callbackAddress = 0x80012340;

        u32 regs[32] = {};
        regs[9] = 0x19;            // HookEntryInt
        regs[4] = callbackAddress; // not a descriptor (heuristic path removed)
        system.callBiosVector(0xB0, regs, 32);

        system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
        system.interrupts().raise(InterruptLine::VBlank);
        system.serviceInterrupts();
        assert(lastCallback == 0);

        std::cerr << "[PASS] B0 HookEntryInt requires descriptor pointer\n";
    }

    // ---------------------------------------------------------------
    // Test 18: C0 vector stubs
    //
    // All C0 stubs should be no-ops and not crash.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        const u32 c0Functions[] = {0x00, 0x01, 0x02, 0x03, 0x07, 0x08,
                                   0x09, 0x0A, 0x0C, 0x12, 0x1C};
        for (u32 func : c0Functions)
        {
            regs[9] = func;
            system.callBiosVector(0xC0, regs, 32);
        }

        std::cerr << "[PASS] C0 stub functions (no crash)\n";
    }

    // ---------------------------------------------------------------
    // Test 18b: B0 ReturnFromException exits callback invocation
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        bool reachedAfterReturnFromException = false;
        system.setCallbackInvoker(
            [&system, &reachedAfterReturnFromException](u32) -> u32
            {
                u32 regs[32] = {};
                regs[9] = 0x17; // ReturnFromException
                system.callBiosVector(0xB0, regs, 32);
                reachedAfterReturnFromException = true;
                return 0;
            });

        system.invokeCallback(0x80012000);
        assert(!reachedAfterReturnFromException);

        std::cerr << "[PASS] B0 ReturnFromException unwinds callback invocation\n";
    }

    // ---------------------------------------------------------------
    // Test 19: Unhandled BIOS call logs a warning
    //
    // Calling an unimplemented function should log a warning.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        bool warningLogged = false;
        system.logger().setMinLevel(LogLevel::Warn);
        system.logger().setCallback(
            [&warningLogged](const psxrecomp::runtime::LogEvent& event)
            {
                if (event.level == LogLevel::Warn && event.category == "bios" &&
                    event.message.find("Unhandled") != std::string::npos)
                {
                    warningLogged = true;
                }
            });

        u32 regs[32] = {};
        regs[9] = 0xFF; // unlikely to be implemented
        system.callBiosVector(0xA0, regs, 32);
        assert(warningLogged);

        std::cerr << "[PASS] unhandled BIOS call logs warning\n";
    }

    // ---------------------------------------------------------------
    // Test 18: A0 vector - setjmp (function 0x13)
    //
    // Should return 0 in $v0.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        constexpr u32 bufferAddress = 0x80014100;
        regs[9] = 0x13;          // setjmp
        regs[4] = bufferAddress; // jmp_buf
        regs[2] = 0xDEAD;        // pre-set $v0
        regs[28] = 0x80017770;   // GP
        regs[29] = 0x80017780;   // SP
        regs[30] = 0x80017790;   // FP
        regs[31] = 0x800177A0;   // RA
        regs[16] = 0x11111111;   // S0
        regs[17] = 0x22222222;   // S1
        regs[23] = 0x88888888;   // S7
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 0); // setjmp returns 0
        assert(system.read<u32>(bufferAddress + 0x00) == 0x800177A0u);
        assert(system.read<u32>(bufferAddress + 0x04) == 0x80017780u);
        assert(system.read<u32>(bufferAddress + 0x08) == 0x80017790u);
        assert(system.read<u32>(bufferAddress + 0x0C) == 0x11111111u);
        assert(system.read<u32>(bufferAddress + 0x10) == 0x22222222u);
        assert(system.read<u32>(bufferAddress + 0x28) == 0x88888888u);
        assert(system.read<u32>(bufferAddress + 0x2C) == 0x80017770u);

        std::cerr << "[PASS] A0 setjmp saves HookEntryInt state\n";
    }

    // ---------------------------------------------------------------
    // Test 18: A0 vector - InitHeap (function 0x39)
    //
    // Should acknowledge without crash.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        regs[9] = 0x39;       // InitHeap
        regs[4] = 0x80100000; // base
        regs[5] = 0x10000;    // size
        system.callBiosVector(0xA0, regs, 32);

        std::cerr << "[PASS] A0 InitHeap\n";
    }

    // ---------------------------------------------------------------
    // Test 19: B0 vector - alloc_kernel_memory (function 0x00)
    //
    // Should return a non-zero address.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        regs[9] = 0x00; // alloc_kernel_memory
        regs[4] = 64;   // size
        system.callBiosVector(0xB0, regs, 32);
        assert(regs[2] != 0);

        std::cerr << "[PASS] B0 alloc_kernel_memory\n";
    }

    // ---------------------------------------------------------------
    // Test 20: A0 vector - free (function 0x34)
    //
    // Should be a no-op stub.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        regs[9] = 0x34; // free
        regs[4] = 0x80100000;
        system.callBiosVector(0xA0, regs, 32);

        std::cerr << "[PASS] A0 free (no-op)\n";
    }
}
