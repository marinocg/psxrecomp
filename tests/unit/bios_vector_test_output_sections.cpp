#include "bios_vector_test_output_sections.h"
#include "bios_test_disc.h"
#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace MemoryMap = psxrecomp::MemoryMap;

void runBiosVectorOutputTests()
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
}
