/**
 * @file bios_vector_test.cpp
 * @brief Tests for PsxSystem::callBiosVector covering A0, B0, and C0 vectors.
 */
#include "bios_vector_test_sections.h"
#include "psxrecomp/runtime/disc.h"
#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace MemoryMap = psxrecomp::MemoryMap;

namespace
{
class TestDisc final : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(psxrecomp::u32, std::span<psxrecomp::u8, 2048> out) override
    {
        std::fill(out.begin(), out.end(), 0);
        return true;
    }

    psxrecomp::u32 userSectorCount() const override
    {
        return 16;
    }
};
} // namespace

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
    // Test 3: A0 vector - strncmp (function 0x18)
    //
    // Compare up to N bytes and stop at terminator or mismatch.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u8* ram = system.getRam();
        std::memcpy(ram + 0x4000, "hello", sizeof("hello"));
        std::memcpy(ram + 0x4100, "help!", sizeof("help!"));

        u32 regs[32] = {};
        regs[9] = 0x18;
        regs[4] = 0x4000;
        regs[5] = 0x4100;
        regs[6] = 3;
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 0);

        regs[6] = 4;
        system.callBiosVector(0xA0, regs, 32);
        assert(static_cast<int>(regs[2]) != 0);

        std::cerr << "[PASS] A0 strncmp\n";
    }

    // ---------------------------------------------------------------
    // Test 4: A0 vector - strcpy (function 0x19)
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
    // Test 5: A0 vector - strlen (function 0x1B)
    //
    // Return the length of the string in bytes excluding the terminator.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u8* ram = system.getRam();
        std::memcpy(ram + 0x6800, "Crash", sizeof("Crash"));

        u32 regs[32] = {};
        regs[9] = 0x1B;
        regs[4] = 0x6800;
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 5);

        std::cerr << "[PASS] A0 strlen\n";
    }

    // ---------------------------------------------------------------
    // Test 6: A0 vector - bcopy (function 0x27)
    //
    // Move bytes between RAM regions, allowing overlap.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u8* ram = system.getRam();
        std::memcpy(ram + 0x6900, "abcdef", sizeof("abcdef"));

        u32 regs[32] = {};
        regs[9] = 0x27;
        regs[4] = 0x6900;
        regs[5] = 0x6902;
        regs[6] = 4;
        system.callBiosVector(0xA0, regs, 32);
        assert(std::memcmp(ram + 0x6902, "abcd", 4) == 0);

        std::cerr << "[PASS] A0 bcopy\n";
    }

    // ---------------------------------------------------------------
    // Test 7: A0 vector - bzero (function 0x28)
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
    // Test 8: A0 vector - memcpy (function 0x2A)
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
    // Test 9: A0 vector - memset (function 0x2B)
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
    // Test 10: A0 vector - malloc (function 0x33)
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
    // Test 11: B0 vector - PAD_init2 (function 0x15)
    //
    // Should acknowledge a two-port pad setup.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u32 regs[32] = {};
        regs[9] = 0x15;
        regs[4] = 0x20000001;
        regs[5] = 0x1000;
        system.callBiosVector(0xB0, regs, 32);
        assert(regs[2] == 2);

        std::cerr << "[PASS] B0 PAD_init2\n";
    }

    // ---------------------------------------------------------------
    // Test 12: A0 vector - putchar (function 0x3C)
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
    // Test 13: A0 vector - puts (function 0x3E)
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
    // Test 14: A0 vector - printf (function 0x3F)
    //
    // Should format common integer/string arguments and return a length.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        assert(system.initialize());

        u8* ram = system.getRam();
        std::memcpy(ram + 0xB100, "Score %d %s", sizeof("Score %d %s"));
        std::memcpy(ram + 0xB120, "Wumpa", sizeof("Wumpa"));

        bool logged = false;
        system.logger().setMinLevel(LogLevel::Info);
        system.logger().setCallback(
            [&logged](const psxrecomp::runtime::LogEvent& event)
            {
                if (event.level == LogLevel::Info && event.category == "bios" &&
                    event.message.find("Score 7 Wumpa") != std::string::npos)
                {
                    logged = true;
                }
            });

        u32 regs[32] = {};
        regs[9] = 0x3F;   // printf
        regs[4] = 0xB100; // format string
        regs[5] = 7;
        regs[6] = 0xB120;
        system.callBiosVector(0xA0, regs, 32);
        assert(logged);
        assert(regs[2] == std::strlen("Score 7 Wumpa"));

        std::cerr << "[PASS] A0 printf\n";
    }

    // ---------------------------------------------------------------
    // Test 15: A0 vector - FlushCache (function 0x44)
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
    // Test 16: A0 vector - send_gpu_linked_list (function 0x4B)
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
    // Test 17: BIOS boot + A0 vector - _96_init (function 0x71)
    //
    // BIOS boot should already prime CD-ROM IRQ enables, and _96_init should
    // preserve that state when libcd calls it again.
    // ---------------------------------------------------------------
    {
        PsxSystem system;
        system.setDisc(std::make_shared<TestDisc>());
        assert(system.initialize());
        assert((system.cdrom().readInterruptEnable() & 0x1Fu) == 0x1Fu);

        system.cdrom().writeCommand(0x01);
        assert((system.cdrom().readInterruptFlags() & 0x07u) == 0x03u);
        assert(system.cdrom().readResponse() == 0x02u);
        system.cdrom().writeInterruptFlags(0x04u);

        u32 regs[32] = {};
        regs[9] = 0x71;
        regs[4] = 0x00012000u;
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 0);
        assert((system.cdrom().readInterruptEnable() & 0x1Fu) == 0x1Fu);
        for (u32 i = 0; i < 5; ++i)
        {
            assert(system.read<u32>(0x00012000u + i * sizeof(u32)) != 0u);
        }

        std::cerr << "[PASS] BIOS boot and A0 _96_init prime CD-ROM IRQ enable\n";
    }

    // ---------------------------------------------------------------
    // Test 18: A0 vector - _96_remove (function 0x72)
    //
    // Should leave BIOS-managed CD-ROM event routing intact.
    // ---------------------------------------------------------------
    {
        using psxrecomp::runtime::EventMode;
        namespace EventClass = psxrecomp::runtime::EventClass;
        namespace EventSpec = psxrecomp::runtime::EventSpec;

        PsxSystem system;
        assert(system.initialize());
        assert((system.cdrom().readInterruptEnable() & 0x1Fu) == 0x1Fu);

        u32 regs[32] = {};
        regs[9] = 0x72;
        system.callBiosVector(0xA0, regs, 32);
        assert(regs[2] == 0);

        bool callbackInvoked = false;
        const u32 handle = system.events().openEvent(EventClass::Cdrom, EventSpec::CommandDone,
                                                     EventMode::Callback, 0x80014000u);
        assert(handle != 0xFFFFFFFFu);
        assert(system.events().enableEvent(handle));
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
        system.cdrom().writeCommand(0x01); // Getstat -> INT3
        system.serviceInterrupts();

        assert(callbackInvoked);
        assert((system.cdrom().readInterruptFlags() & 0x07u) == 0u);

        std::cerr << "[PASS] A0 _96_remove leaves BIOS CD-ROM routing intact\n";
    }

    // ---------------------------------------------------------------
    // Test 19: A0 vector - gpu_sync (function 0x4E)
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

    runBiosVectorKernelEventTests();
    runBiosVectorInterruptChainTests();

    std::cerr << "All BIOS vector tests passed.\n";
    return 0;
}
