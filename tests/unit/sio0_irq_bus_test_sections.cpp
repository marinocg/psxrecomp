#include "psxrecomp/runtime/input.h"
#include "psxrecomp/runtime/psx_system.h"
#include "sio0_test_sections.h"

#include <cassert>
#include <cstdio>

namespace Mmio = psxrecomp::runtime::Mmio;

void runSio0IrqBusTests(psxrecomp::runtime::PsxSystem& system)
{
    using psxrecomp::Address;
    using psxrecomp::runtime::ControllerButton;
    using psxrecomp::runtime::InputController;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::PsxSystem;
    using psxrecomp::runtime::Sio0;

    auto resetForTransfer = [&]()
    {
        constexpr Address joyCtrl = 0x1F80104Au;
        // Full SIO0 reset.
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0040u);
        // Select port 1, enable TX, assert /JOY.
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0003u);
    };

    // ================================================================
    //  ACK / IRQ delivery tests
    // ================================================================

    // Helper: reset SIO0 and set up for an IRQ-enabled transfer.
    // CTRL = TX enable (bit 0) | /JOY select (bit 1) | IRQ enable (bit 12).
    auto resetForIrqTransfer = [&]()
    {
        constexpr Address joyCtrl = 0x1F80104Au;
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0040u); // full reset
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x1003u); // port1 + IRQ en
        // Clear any previous Controller IRQ in I_STAT.
        system.interrupts().writeStatus(
            ~static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller));
    };

    // ----------------------------------------------------------------
    // 17. ACK-backed transfer step schedules IRQ; STAT.9 set after tick.
    // ----------------------------------------------------------------
    {
        resetForIrqTransfer();

        constexpr Address joyData = 0x1F801040u;
        constexpr Address joyStat = 0x1F801044u;

        // Send address byte 0x01 — the device ACKs.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);

        // Immediately after write, STAT.7 (ACK level) should be set
        // but STAT.9 (IRQ Request) should NOT yet be set — it's delayed.
        {
            auto st = system.readMmioExplicit<psxrecomp::u32>(joyStat);
            assert(st & (1u << 7));    // ACK level asserted
            assert(!(st & (1u << 9))); // IRQ not yet
        }

        // Tick the scheduler past the ACK delay.
        system.tickCpuCycles(psxrecomp::runtime::Sio0::ACK_DELAY_CYCLES + 1);

        // Now STAT.9 should be set and STAT.7 should be cleared.
        {
            auto st = system.readMmioExplicit<psxrecomp::u32>(joyStat);
            assert(st & (1u << 9));    // IRQ request set
            assert(!(st & (1u << 7))); // ACK level cleared by IRQ firing
        }

        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] ACK schedules delayed IRQ; STAT.9 set after tick");
    }

    // ----------------------------------------------------------------
    // 18. Controller IRQ (bit 7) appears in I_STAT after ACK delay.
    // ----------------------------------------------------------------
    {
        resetForIrqTransfer();

        constexpr Address joyData = 0x1F801040u;

        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);

        // I_STAT Controller bit should be clear before tick.
        {
            auto istat = system.interrupts().readStatus();
            assert(!(istat &
                     static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller)));
        }

        system.tickCpuCycles(psxrecomp::runtime::Sio0::ACK_DELAY_CYCLES + 1);

        // Now Controller bit should be set in I_STAT.
        {
            auto istat = system.interrupts().readStatus();
            assert(istat &
                   static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller));
        }

        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] Controller IRQ appears in I_STAT after ACK delay");
    }

    // ----------------------------------------------------------------
    // 19. JOY_CTRL.4 (acknowledge) clears STAT.9 (IRQ request).
    // ----------------------------------------------------------------
    {
        resetForIrqTransfer();

        constexpr Address joyData = 0x1F801040u;
        constexpr Address joyCtrl = 0x1F80104Au;
        constexpr Address joyStat = 0x1F801044u;

        // Trigger an ACK.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        system.tickCpuCycles(psxrecomp::runtime::Sio0::ACK_DELAY_CYCLES + 1);
        assert(system.readMmioExplicit<psxrecomp::u32>(joyStat) & (1u << 9));

        // Write CTRL with bit 4 (acknowledge) to clear STAT.9.
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x1013u); // keep IRQ en + ack
        {
            auto st = system.readMmioExplicit<psxrecomp::u32>(joyStat);
            assert(!(st & (1u << 9))); // IRQ request cleared
        }

        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] CTRL.4 acknowledge clears STAT.9");
    }

    // ----------------------------------------------------------------
    // 20. No stuck IRQ bit after acknowledge + I_STAT clear.
    // ----------------------------------------------------------------
    {
        resetForIrqTransfer();

        constexpr Address joyData = 0x1F801040u;
        constexpr Address joyCtrl = 0x1F80104Au;

        // Trigger ACK + IRQ.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        system.tickCpuCycles(psxrecomp::runtime::Sio0::ACK_DELAY_CYCLES + 1);

        // Acknowledge in SIO0.
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x1013u);

        // Clear Controller bit in I_STAT.
        system.interrupts().writeStatus(
            ~static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller));

        // Tick again — IRQ should NOT re-appear.
        system.tickCpuCycles(500);
        {
            auto istat = system.interrupts().readStatus();
            assert(!(istat &
                     static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller)));
        }
        // STAT.9 should still be clear.
        assert(!(system.sio0().stat() & (1u << 9)));

        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] No stuck IRQ bit after clear");
    }

    // ----------------------------------------------------------------
    // 21. No IRQ when CTRL IRQ enable (bit 12) is off.
    // ----------------------------------------------------------------
    {
        // Use the non-IRQ resetForTransfer helper (CTRL = 0x0003, no bit 12).
        resetForTransfer();

        constexpr Address joyData = 0x1F801040u;

        // Clear Controller bit in I_STAT.
        system.interrupts().writeStatus(
            ~static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller));

        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        system.tickCpuCycles(psxrecomp::runtime::Sio0::ACK_DELAY_CYCLES + 1);

        // STAT.9 should NOT be set (IRQ enable was off).
        assert(!(system.sio0().stat() & (1u << 9)));
        // I_STAT Controller bit should NOT be set.
        {
            auto istat = system.interrupts().readStatus();
            assert(!(istat &
                     static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller)));
        }

        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] No IRQ when CTRL IRQ enable is off");
    }

    // ----------------------------------------------------------------
    // 22. Last byte of transfer (buttons_high) produces no ACK / no IRQ.
    // ----------------------------------------------------------------
    {
        resetForIrqTransfer();

        constexpr Address joyData = 0x1F801040u;

        // Clear I_STAT Controller.
        system.interrupts().writeStatus(
            ~static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller));

        // Run a full 5-byte exchange, ticking between each byte
        // to let pending ACK IRQs fire and then clearing them.
        auto sendAndTick = [&](psxrecomp::u8 tx)
        {
            system.writeMmioExplicit<psxrecomp::u8>(joyData, tx);
            system.tickCpuCycles(psxrecomp::runtime::Sio0::ACK_DELAY_CYCLES + 1);
            // Acknowledge any STAT.9.
            constexpr Address joyCtrl = 0x1F80104Au;
            system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x1013u);
            // Clear I_STAT Controller.
            system.interrupts().writeStatus(
                ~static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller));
            (void)system.readMmioExplicit<psxrecomp::u8>(joyData);
        };

        sendAndTick(0x01u); // address
        sendAndTick(0x42u); // command
        sendAndTick(0x00u); // 0x5A
        sendAndTick(0x00u); // buttons_lo

        // Now send last byte (buttons_hi) — no ACK expected.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x00u);
        system.tickCpuCycles(psxrecomp::runtime::Sio0::ACK_DELAY_CYCLES + 1);

        // STAT.9 should NOT be set (no ACK on last byte).
        assert(!(system.sio0().stat() & (1u << 9)));
        // I_STAT Controller should NOT be set.
        {
            auto istat = system.interrupts().readStatus();
            assert(!(istat &
                     static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller)));
        }

        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] Last byte produces no ACK / no IRQ");
    }

    // ----------------------------------------------------------------
    // 23. OpenEvent(Controller, Interrupted) callback path observes IRQ.
    // ----------------------------------------------------------------
    {
        resetForIrqTransfer();

        // Enable I_MASK for Controller so serviceInterrupts() can dispatch.
        system.interrupts().writeMask(
            static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller));

        // Open and enable a kernel event for the Controller interrupt class.
        namespace EventClass = psxrecomp::runtime::EventClass;
        namespace EventSpec = psxrecomp::runtime::EventSpec;
        using psxrecomp::runtime::EventMode;
        using psxrecomp::runtime::EventStatus;

        auto& events = system.events();
        psxrecomp::u32 handle = events.openEvent(EventClass::Controller, EventSpec::Interrupted,
                                                 EventMode::NoCallback, 0);
        assert(handle != 0xFFFFFFFFu);
        assert(events.enableEvent(handle));

        constexpr Address joyData = 0x1F801040u;

        // Trigger an ACK-backed transfer byte.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        system.tickCpuCycles(psxrecomp::runtime::Sio0::ACK_DELAY_CYCLES + 1);

        // Service interrupts — should dispatch Controller line and deliver event.
        system.serviceInterrupts();

        // The NoCallback event should now be in Delivered state.
        assert(events.testEvent(handle) == 1);

        // Clean up.
        events.closeEvent(handle);
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] OpenEvent(Controller, Interrupted) observes IRQ");
    }

    // ================================================================
    //  Device-bus routing tests (address-byte dispatch)
    // ================================================================

    // ----------------------------------------------------------------
    // 24. Memory-card address 0x81 → Hi-Z, no ACK (not implemented).
    // ----------------------------------------------------------------
    {
        resetForTransfer();

        constexpr Address joyData = 0x1F801040u;
        constexpr Address joyStat = 0x1F801044u;

        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x81u);
        assert(system.sio0().rxData() == 0xFFu);
        {
            auto st = system.readMmioExplicit<psxrecomp::u32>(joyStat);
            assert(st & (1u << 1));    // RX not empty
            assert(!(st & (1u << 7))); // no ACK from memory card
        }
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] Memory-card address 0x81 → no ACK");
    }

    // ----------------------------------------------------------------
    // 25. After 0x81 (no ACK), a subsequent 0x01 still selects controller.
    // ----------------------------------------------------------------
    {
        resetForTransfer();

        constexpr Address joyData = 0x1F801040u;
        constexpr Address joyStat = 0x1F801044u;

        // First, address the memory card → no ACK.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x81u);
        assert(!(system.readMmioExplicit<psxrecomp::u32>(joyStat) & (1u << 7)));
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        // Deassert and reassert /JOY for a fresh transfer.
        resetForTransfer();

        // Now address the controller → should ACK normally.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        assert(system.sio0().rxData() == 0xFFu);
        {
            auto st = system.readMmioExplicit<psxrecomp::u32>(joyStat);
            assert(st & (1u << 7)); // ACK from controller
        }
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] Controller works after memory-card no-ACK");
    }

    // ----------------------------------------------------------------
    // 26. Port 2 + memory-card address → still disconnected.
    // ----------------------------------------------------------------
    {
        constexpr Address joyCtrl = 0x1F80104Au;
        constexpr Address joyData = 0x1F801040u;
        constexpr Address joyStat = 0x1F801044u;

        // Reset, then select port 2.
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0040u);
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x2003u);

        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x81u);
        assert(system.sio0().rxData() == 0xFFu);
        {
            auto st = system.readMmioExplicit<psxrecomp::u32>(joyStat);
            assert(!(st & (1u << 7))); // no ACK from absent port
        }
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] Port 2 + memory-card address → disconnected");
    }

    // ----------------------------------------------------------------
    // 27. Bytes sent after 0x81 (no ACK) are ignored until /JOY reset.
    // ----------------------------------------------------------------
    {
        resetForTransfer();

        constexpr Address joyData = 0x1F801040u;
        constexpr Address joyStat = 0x1F801044u;

        // Address memory card → no ACK.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x81u);
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        // Send more bytes — they should all return 0xFF, no ACK.
        for (psxrecomp::u8 b : {0x42u, 0x00u, 0x00u})
        {
            system.writeMmioExplicit<psxrecomp::u8>(joyData, b);
            assert(system.sio0().rxData() == 0xFFu);
            assert(!(system.readMmioExplicit<psxrecomp::u32>(joyStat) & (1u << 7)));
            (void)system.readMmioExplicit<psxrecomp::u8>(joyData);
        }

        std::puts("[PASS] Bytes after 0x81 ignored until /JOY reset");
    }

    // ----------------------------------------------------------------
    // 28. 0x81 with IRQ enabled does NOT raise an IRQ.
    // ----------------------------------------------------------------
    {
        resetForIrqTransfer();

        constexpr Address joyData = 0x1F801040u;

        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x81u);
        system.tickCpuCycles(psxrecomp::runtime::Sio0::ACK_DELAY_CYCLES + 1);

        // STAT.9 should NOT be set.
        assert(!(system.sio0().stat() & (1u << 9)));
        // I_STAT Controller should NOT be set.
        {
            auto istat = system.interrupts().readStatus();
            assert(!(istat &
                     static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::Controller)));
        }

        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] 0x81 with IRQ enabled → no IRQ");
    }
}
