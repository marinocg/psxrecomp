/**
 * @file bios_vector_test.cpp
 * @brief Tests for PsxSystem::callBiosVector covering A0, B0, and C0 vectors.
 */
#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace MemoryMap = psxrecomp::MemoryMap;

int main()
{
    using psxrecomp::Address;
    using psxrecomp::u32;
    using psxrecomp::u8;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::LogLevel;
    using psxrecomp::runtime::PsxSystem;
    namespace EventClass = psxrecomp::runtime::EventClass;
    namespace EventSpec = psxrecomp::runtime::EventSpec;
    using psxrecomp::runtime::EventMode;

    // ---------------------------------------------------------------
    // Test 1: Guard against insufficient register file
    //
    // callBiosVector with regCount < 32 should log a warning but not crash.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        bool warningLogged = false;
        system.logger().setMinLevel(LogLevel::Warn);
        system.logger().setCallback(
            [&warningLogged](const psxrecomp::runtime::LogEvent& event)
            {
                if (event.level == LogLevel::Warn && event.category == "bios")
                {
                    warningLogged = true;
                }
            });

        u32 shortRegs[4] = {};
        system.callBiosVector(0xA0, shortRegs, 4);
        assert(warningLogged);

        std::cerr << "[PASS] guard against insufficient register file\n";
    }

    // ---------------------------------------------------------------
    // Test 2: A0 vector - strcmp (function 0x17)
    //
    // Compare two strings in RAM and return result in $v0 (reg 2).
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u8* ram = system.getRam();
        // Place "hello" at offset 0x1000
        std::memcpy(ram + 0x1000, "hello", sizeof("hello"));
        // Place "hello" at offset 0x2000
        std::memcpy(ram + 0x2000, "hello", sizeof("hello"));
        // Place "world" at offset 0x3000
        std::memcpy(ram + 0x3000, "world", sizeof("world"));

        u32 regs[32] = {};
        regs[9] = 0x17;   // $t1 = function id (strcmp)
        regs[4] = 0x1000; // $a0 = address of "hello"
        regs[5] = 0x2000; // $a1 = address of "hello"
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 0); // equal strings

        regs[5] = 0x3000; // $a1 = address of "world"
        system.callBiosVector(0xA0, regs, 32);
        assert(static_cast<int>(regs[2]) != 0); // different strings

        std::cerr << "[PASS] A0 strcmp\n";
    }

    // ---------------------------------------------------------------
    // Test 3: A0 vector - strcpy (function 0x19)
    //
    // Copy a string and return destination address in $v0.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u8* ram = system.getRam();
        std::memcpy(ram + 0x5000, "psxrecomp", sizeof("psxrecomp"));
        std::memset(ram + 0x6000, 0xFF, 20);

        u32 regs[32] = {};
        regs[9] = 0x19;   // strcpy
        regs[4] = 0x6000; // $a0 = dst
        regs[5] = 0x5000; // $a1 = src
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 0x6000); // returns dst
        assert(std::strcmp(reinterpret_cast<const char*>(ram + 0x6000), "psxrecomp") == 0);

        std::cerr << "[PASS] A0 strcpy\n";
    }

    // ---------------------------------------------------------------
    // Test 4: A0 vector - bzero (function 0x28)
    //
    // Zero a region of RAM.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u8* ram = system.getRam();
        std::memset(ram + 0x7000, 0xAA, 16);

        u32 regs[32] = {};
        regs[9] = 0x28;   // bzero
        regs[4] = 0x7000; // $a0 = address
        regs[5] = 16;     // $a1 = length
        system.callBiosVector(0xA0, regs, 32);

        for (int i = 0; i < 16; ++i)
        {
            assert(ram[0x7000 + i] == 0);
        }

        std::cerr << "[PASS] A0 bzero\n";
    }

    // ---------------------------------------------------------------
    // Test 5: A0 vector - memcpy (function 0x2A)
    //
    // Copy bytes between RAM regions.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u8* ram = system.getRam();
        ram[0x8000] = 0x11;
        ram[0x8001] = 0x22;
        ram[0x8002] = 0x33;
        ram[0x8003] = 0x44;

        u32 regs[32] = {};
        regs[9] = 0x2A;   // memcpy
        regs[4] = 0x9000; // $a0 = dst
        regs[5] = 0x8000; // $a1 = src
        regs[6] = 4;      // $a2 = size
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 0x9000); // returns dst
        assert(ram[0x9000] == 0x11);
        assert(ram[0x9001] == 0x22);
        assert(ram[0x9002] == 0x33);
        assert(ram[0x9003] == 0x44);

        std::cerr << "[PASS] A0 memcpy\n";
    }

    // ---------------------------------------------------------------
    // Test 6: A0 vector - memset (function 0x2B)
    //
    // Fill a region with a byte value.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u8* ram = system.getRam();
        std::memset(ram + 0xA000, 0x00, 8);

        u32 regs[32] = {};
        regs[9] = 0x2B;   // memset
        regs[4] = 0xA000; // $a0 = dst
        regs[5] = 0x55;   // $a1 = value
        regs[6] = 8;      // $a2 = size
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 0xA000);
        for (int i = 0; i < 8; ++i)
        {
            assert(ram[0xA000 + i] == 0x55);
        }

        std::cerr << "[PASS] A0 memset\n";
    }

    // ---------------------------------------------------------------
    // Test 7: A0 vector - malloc (function 0x33)
    //
    // Simple bump allocator should return a non-zero address.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        regs[9] = 0x33; // malloc
        regs[4] = 256;  // $a0 = size
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] != 0); // should allocate
        u32 firstAlloc = regs[2];

        // Second allocation should be at a different address
        regs[9] = 0x33;
        regs[4] = 128;
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] != 0);
        assert(regs[2] != firstAlloc);

        std::cerr << "[PASS] A0 malloc\n";
    }

    // ---------------------------------------------------------------
    // Test 8: A0 vector - putchar (function 0x3C)
    //
    // Should log the character without crashing.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        bool logged = false;
        system.logger().setMinLevel(LogLevel::Info);
        system.logger().setCallback(
            [&logged](const psxrecomp::runtime::LogEvent& event)
            {
                if (event.level == LogLevel::Info && event.category == "bios" &&
                    event.message.find("putchar") != std::string::npos)
                {
                    logged = true;
                }
            });

        u32 regs[32] = {};
        regs[9] = 0x3C; // putchar
        regs[4] = 'X';  // $a0 = character
        system.callBiosVector(0xA0, regs, 32);
        assert(logged);

        std::cerr << "[PASS] A0 putchar\n";
    }

    // ---------------------------------------------------------------
    // Test 9: A0 vector - puts (function 0x3E)
    //
    // Should log the string without crashing.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u8* ram = system.getRam();
        std::memcpy(ram + 0xB000, "Hello BIOS!", sizeof("Hello BIOS!"));

        bool logged = false;
        system.logger().setMinLevel(LogLevel::Info);
        system.logger().setCallback(
            [&logged](const psxrecomp::runtime::LogEvent& event)
            {
                if (event.level == LogLevel::Info && event.category == "bios" &&
                    event.message.find("Hello BIOS!") != std::string::npos)
                {
                    logged = true;
                }
            });

        u32 regs[32] = {};
        regs[9] = 0x3E;   // puts
        regs[4] = 0xB000; // $a0 = string address
        system.callBiosVector(0xA0, regs, 32);
        assert(logged);

        std::cerr << "[PASS] A0 puts\n";
    }

    // ---------------------------------------------------------------
    // Test 10: A0 vector - FlushCache (function 0x44)
    //
    // Should be a no-op (no crash).
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        regs[9] = 0x44; // FlushCache
        system.callBiosVector(0xA0, regs, 32);
        // If we get here, it didn't crash

        std::cerr << "[PASS] A0 FlushCache (no-op)\n";
    }

    // ---------------------------------------------------------------
    // Test 11: A0 vector - send_gpu_linked_list (function 0x4B)
    //
    // Should configure GPU DMA via registers and consume linked-list words.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        constexpr Address otBase = 0x00012000;
        system.write<u32>(otBase + 0x0, (2u << 24) | 0x00FFFFFFu);
        system.write<u32>(otBase + 0x4, 0xE1000000u);
        system.write<u32>(otBase + 0x8, 0x20010203u);

        u32 regs[32] = {};
        regs[9] = 0x4B;
        regs[4] = otBase;
        system.callBiosVector(0xA0, regs, 32);

        const u32 gpuDmaChcr = system.read<u32>(0x1F8010A8u);
        const u32 dmaDicr = system.read<u32>(0x1F8010F4u);
        const u32 gpuStat = system.read<u32>(0x1F801814u);

        // Transfer completed (start bit cleared), DMA2 completion latched.
        assert((gpuDmaChcr & 0x01000000u) == 0);
        assert((dmaDicr & (1u << 26)) != 0);
        assert(((gpuStat >> 29) & 0x3u) == 0x2u);

        std::cerr << "[PASS] A0 send_gpu_linked_list uses DMA register path\n";
    }

    // ---------------------------------------------------------------
    // Test 12: A0 vector - gpu_sync (function 0x4E)
    //
    // Should disable GPU DMA mode after synchronization.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        constexpr Address otBase = 0x00012200;
        system.write<u32>(otBase + 0x0, (1u << 24) | 0x00FFFFFFu);
        system.write<u32>(otBase + 0x4, 0xE1000000u);

        u32 regs[32] = {};
        regs[9] = 0x4B;
        regs[4] = otBase;
        system.callBiosVector(0xA0, regs, 32);

        regs[9] = 0x4E;
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 0);

        const u32 gpuStat = system.read<u32>(0x1F801814u);
        assert(((gpuStat >> 29) & 0x3u) == 0u);

        std::cerr << "[PASS] A0 gpu_sync disables DMA mode\n";
    }

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
    // Test 15: B0 vector stubs (InitPad, StartPad, etc.)
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
    // Test 16: B0 HookEntryInt descriptor callback runs on IRQ service
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
        system.write<u32>(descriptorAddress, callbackAddress);

        u32 regs[32] = {};
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
        regs[9] = 0x13;   // setjmp
        regs[2] = 0xDEAD; // pre-set $v0
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 0); // setjmp returns 0

        std::cerr << "[PASS] A0 setjmp returns 0\n";
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
    // Test 24: ReturnFromException in chain short-circuits event dispatch
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

        assert(order.size() == 1);
        assert(order[0] == func1);
        assert((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::VBlank)) != 0);
        std::cerr << "[PASS] chain ReturnFromException short-circuits dispatch\n";
    }

    // ---------------------------------------------------------------
    // Test 25: ReturnFromException in HookEntryInt short-circuits dispatcher
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        constexpr u32 hookDescriptor = 0x80017300;
        constexpr u32 hookCallback = 0x80016300;
        constexpr u32 eventCallback = 0x80016310;

        system.write<u32>(hookDescriptor, hookCallback);
        u32 regs[32] = {};
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
                    u32 regs[32] = {};
                    regs[9] = 0x17; // ReturnFromException
                    system.callBiosVector(0xB0, regs, 32);
                }
                return 0;
            });

        system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
        system.interrupts().raise(InterruptLine::VBlank);
        system.serviceInterrupts();

        assert(order.size() == 1);
        assert(order[0] == hookCallback);
        assert((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::VBlank)) != 0);
        std::cerr << "[PASS] HookEntryInt ReturnFromException short-circuits dispatcher\n";
    }

    std::cerr << "All BIOS vector tests passed.\n";
    return 0;
}
