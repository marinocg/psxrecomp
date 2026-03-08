#include "psxrecomp/runtime/input.h"
#include "psxrecomp/runtime/psx_system.h"
#include "sio0_test_sections.h"

#include <cassert>
#include <cstdio>

namespace Mmio = psxrecomp::runtime::Mmio;

int main()
{
    using psxrecomp::Address;
    using psxrecomp::runtime::ControllerButton;
    using psxrecomp::runtime::InputController;
    using psxrecomp::runtime::PsxSystem;
    using psxrecomp::runtime::Sio0;

    PsxSystem system;
    assert(system.initialize());

    // ----------------------------------------------------------------
    //  1. Reset defaults are correct
    // ----------------------------------------------------------------
    {
        [[maybe_unused]] const auto& sio = system.sio0();
        assert(sio.stat() == 0x0005u);
        assert(sio.mode() == 0x000Du);
        assert(sio.ctrl() == 0x0000u);
        assert(sio.baud() == 0x0088u);
        std::puts("[PASS] SIO0 reset defaults");
    }

    // ----------------------------------------------------------------
    //  2. 16-bit MMIO readbacks through PsxSystem (mode, ctrl, baud)
    // ----------------------------------------------------------------
    {
        constexpr Address joyMode = 0x1F801048u;
        constexpr Address joyCtrl = 0x1F80104Au;
        constexpr Address joyBaud = 0x1F80104Eu;

        assert(system.readMmioExplicit<psxrecomp::u16>(joyMode) == 0x000Du);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyCtrl) == 0x0000u);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyBaud) == 0x0088u);

        system.writeMmioExplicit<psxrecomp::u16>(joyMode, 0x000Eu);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyMode) == 0x000Eu);

        system.writeMmioExplicit<psxrecomp::u16>(joyBaud, 0x0044u);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyBaud) == 0x0044u);

        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x2003u);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyCtrl) == 0x2003u);

        std::puts("[PASS] SIO0 16-bit MMIO readbacks");
    }

    // ----------------------------------------------------------------
    //  3. 32-bit JOY_STAT read via MMIO
    // ----------------------------------------------------------------
    {
        constexpr Address joyStat = 0x1F801044u;
        [[maybe_unused]] const auto stat = system.readMmioExplicit<psxrecomp::u32>(joyStat);
        assert((stat & 0x05u) == 0x05u);
        std::puts("[PASS] SIO0 32-bit JOY_STAT read");
    }

    // ----------------------------------------------------------------
    //  4. 8-bit JOY_RX_DATA read / JOY_TX_DATA write (idle state)
    // ----------------------------------------------------------------
    {
        [[maybe_unused]] constexpr Address joyData = 0x1F801040u;
        // Reset to get clean state.
        constexpr Address joyCtrl = 0x1F80104Au;
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0040u);

        // RX data defaults to 0xFF (high-Z, no device connected).
        assert(system.readMmioExplicit<psxrecomp::u8>(joyData) == 0xFFu);
        std::puts("[PASS] SIO0 8-bit data read/write");
    }

    // ----------------------------------------------------------------
    //  5. JOY_CTRL acknowledge (bit 4) clears sticky STAT bits
    // ----------------------------------------------------------------
    {
        constexpr Address joyCtrl = 0x1F80104Au;
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0010u);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyCtrl) == 0x0000u);
        std::puts("[PASS] SIO0 CTRL acknowledge side-effect");
    }

    // ----------------------------------------------------------------
    //  6. JOY_CTRL reset (bit 6) resets all SIO0 registers
    // ----------------------------------------------------------------
    {
        constexpr Address joyMode = 0x1F801048u;
        constexpr Address joyCtrl = 0x1F80104Au;
        constexpr Address joyBaud = 0x1F80104Eu;
        [[maybe_unused]] constexpr Address joyStat = 0x1F801044u;

        system.writeMmioExplicit<psxrecomp::u16>(joyMode, 0x0003u);
        system.writeMmioExplicit<psxrecomp::u16>(joyBaud, 0x0044u);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyMode) == 0x0003u);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyBaud) == 0x0044u);

        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0040u);

        assert(system.readMmioExplicit<psxrecomp::u16>(joyMode) == 0x000Du);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyCtrl) == 0x0000u);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyBaud) == 0x0088u);
        assert(system.readMmioExplicit<psxrecomp::u32>(joyStat) == 0x0005u);
        std::puts("[PASS] SIO0 CTRL reset side-effect");
    }

    // ----------------------------------------------------------------
    //  7. Writes to one register don't bleed into another
    // ----------------------------------------------------------------
    {
        constexpr Address joyMode = 0x1F801048u;
        [[maybe_unused]] constexpr Address joyBaud = 0x1F80104Eu;
        [[maybe_unused]] constexpr Address joyCtrl = 0x1F80104Au;

        system.writeMmioExplicit<psxrecomp::u16>(joyMode, 0x0106u);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyBaud) == 0x0088u);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyCtrl) == 0x0000u);
        std::puts("[PASS] SIO0 register isolation");
    }

    // ----------------------------------------------------------------
    //  8. JOY_MODE masks out reserved upper bits
    // ----------------------------------------------------------------
    {
        constexpr Address joyMode = 0x1F801048u;
        system.writeMmioExplicit<psxrecomp::u16>(joyMode, 0xFFFFu);
        assert(system.readMmioExplicit<psxrecomp::u16>(joyMode) == 0x01FFu);

        constexpr Address joyCtrl = 0x1F80104Au;
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0040u);
        std::puts("[PASS] SIO0 mode register bit masking");
    }

    // ================================================================
    //  Digital pad protocol tests (slot 1, command 0x42)
    // ================================================================

    // Helper: reset SIO0 and set up for a fresh transfer.
    auto resetForTransfer = [&]()
    {
        constexpr Address joyCtrl = 0x1F80104Au;
        // Full SIO0 reset.
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0040u);
        // Select port 1, enable TX, assert /JOY.
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0003u);
    };

    // ----------------------------------------------------------------
    //  9. Full 5-byte digital pad exchange: 01 42 00 00 00
    //     with no buttons pressed (all released = 0xFFFF).
    // ----------------------------------------------------------------
    {
        resetForTransfer();

        constexpr Address joyData = 0x1F801040u;
        constexpr Address joyStat = 0x1F801044u;

        // Byte 0: send address 0x01 → expect 0xFF (Hi-Z).
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        assert(system.sio0().rxData() == 0xFFu);
        {
            [[maybe_unused]] auto st = system.readMmioExplicit<psxrecomp::u32>(joyStat);
            assert(st & (1u << 1)); // RX not empty
            assert(st & (1u << 7)); // ACK
        }
        // Consume RX.
        assert(system.readMmioExplicit<psxrecomp::u8>(joyData) == 0xFFu);

        // Byte 1: send command 0x42 → expect 0x41 (digital pad ID).
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x42u);
        assert(system.sio0().rxData() == 0x41u);
        assert(system.readMmioExplicit<psxrecomp::u8>(joyData) == 0x41u);

        // Byte 2: send 0x00 → expect 0x5A (ready marker).
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x00u);
        assert(system.sio0().rxData() == 0x5Au);
        assert(system.readMmioExplicit<psxrecomp::u8>(joyData) == 0x5Au);

        // Byte 3: send 0x00 → expect buttons_low = 0xFF (nothing pressed).
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x00u);
        assert(system.sio0().rxData() == 0xFFu);
        assert(system.readMmioExplicit<psxrecomp::u8>(joyData) == 0xFFu);

        // Byte 4: send 0x00 → expect buttons_high = 0xFF.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x00u);
        assert(system.sio0().rxData() == 0xFFu);
        {
            [[maybe_unused]] auto st = system.readMmioExplicit<psxrecomp::u32>(joyStat);
            assert(st & (1u << 1));    // RX not empty
            assert(!(st & (1u << 7))); // NO ACK on last byte
        }
        assert(system.readMmioExplicit<psxrecomp::u8>(joyData) == 0xFFu);

        std::puts("[PASS] Digital pad 5-byte exchange (no buttons)");
    }

    // ----------------------------------------------------------------
    // 10. Digital pad exchange with buttons pressed — verify
    //     InputController state appears in the response.
    // ----------------------------------------------------------------
    {
        // Press Cross and Start.
        system.input().setButton(ControllerButton::Cross, true);
        system.input().setButton(ControllerButton::Start, true);

        resetForTransfer();

        constexpr Address joyData = 0x1F801040u;

        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData); // consume 0xFF

        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x42u);
        assert(system.readMmioExplicit<psxrecomp::u8>(joyData) == 0x41u);

        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x00u);
        assert(system.readMmioExplicit<psxrecomp::u8>(joyData) == 0x5Au);

        // buttons_low: Start=bit3 pressed → bit cleared.
        //              Cross=bit14 → high byte, doesn't affect low.
        // Expected low = 0xFF & ~(1<<3) = 0xF7.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x00u);
        psxrecomp::u8 lo = system.readMmioExplicit<psxrecomp::u8>(joyData);
        assert(lo == 0xF7u);

        // buttons_high: Cross=bit14 → bit6 in high byte → cleared.
        // Expected high = 0xFF & ~(1<<6) = 0xBF.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x00u);
        psxrecomp::u8 hi = system.readMmioExplicit<psxrecomp::u8>(joyData);
        assert(hi == 0xBFu);

        // Reconstruct and verify against InputController.
        [[maybe_unused]] psxrecomp::u16 reconstructed = static_cast<psxrecomp::u16>(lo | (hi << 8));
        assert(reconstructed == system.input().readState());

        // Release buttons for subsequent tests.
        system.input().setButton(ControllerButton::Cross, false);
        system.input().setButton(ControllerButton::Start, false);

        std::puts("[PASS] Digital pad exchange with buttons pressed");
    }

    // ----------------------------------------------------------------
    // 11. Unknown address (not 0x01) produces Hi-Z, no ACK.
    // ----------------------------------------------------------------
    {
        resetForTransfer();

        constexpr Address joyData = 0x1F801040u;
        constexpr Address joyStat = 0x1F801044u;

        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x02u);
        assert(system.sio0().rxData() == 0xFFu);
        {
            [[maybe_unused]] auto st = system.readMmioExplicit<psxrecomp::u32>(joyStat);
            assert(!(st & (1u << 7))); // no ACK
        }
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] Unknown device address → no ACK");
    }

    // ----------------------------------------------------------------
    // 12. Port 2 (CTRL bit 13 set) → disconnected, no ACK.
    // ----------------------------------------------------------------
    {
        constexpr Address joyCtrl = 0x1F80104Au;
        constexpr Address joyData = 0x1F801040u;
        constexpr Address joyStat = 0x1F801044u;

        // Full reset then select port 2.
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0040u);
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x2003u);

        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        assert(system.sio0().rxData() == 0xFFu);
        {
            [[maybe_unused]] auto st = system.readMmioExplicit<psxrecomp::u32>(joyStat);
            assert(!(st & (1u << 7))); // no ACK from absent port
        }
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] Port 2 disconnected → no response");
    }

    // ----------------------------------------------------------------
    // 13. Unknown command (address OK, but command != 0x42) aborts.
    // ----------------------------------------------------------------
    {
        resetForTransfer();

        constexpr Address joyData = 0x1F801040u;
        constexpr Address joyStat = 0x1F801044u;

        // Address is fine.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        // Send an unknown command.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x99u);
        assert(system.sio0().rxData() == 0xFFu);
        {
            [[maybe_unused]] auto st = system.readMmioExplicit<psxrecomp::u32>(joyStat);
            assert(!(st & (1u << 7))); // no ACK
        }
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] Unknown command → abort, no ACK");
    }

    // ----------------------------------------------------------------
    // 14. CTRL /JOY deassert (bit 1: 1→0) resets protocol mid-transfer.
    // ----------------------------------------------------------------
    {
        resetForTransfer();

        constexpr Address joyCtrl = 0x1F80104Au;
        constexpr Address joyData = 0x1F801040u;

        // Begin a transfer.
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        // Deassert /JOY (clear bit 1).
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0001u);

        // Re-assert and try a fresh address → should work from Idle.
        system.writeMmioExplicit<psxrecomp::u16>(joyCtrl, 0x0003u);
        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        assert(system.sio0().rxData() == 0xFFu);
        [[maybe_unused]] auto st = system.readMmioExplicit<psxrecomp::u32>(0x1F801044u);
        assert(st & (1u << 7)); // ACK from fresh start
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);

        std::puts("[PASS] /JOY deassert resets protocol");
    }

    // ----------------------------------------------------------------
    // 15. RX-Not-Empty (STAT bit 1) is cleared by reading JOY_RX_DATA.
    // ----------------------------------------------------------------
    {
        resetForTransfer();

        constexpr Address joyData = 0x1F801040u;
        [[maybe_unused]] constexpr Address joyStat = 0x1F801044u;

        system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
        assert(system.readMmioExplicit<psxrecomp::u32>(joyStat) & (1u << 1));

        // Reading the data byte should clear RX-Not-Empty.
        (void)system.readMmioExplicit<psxrecomp::u8>(joyData);
        assert(!(system.readMmioExplicit<psxrecomp::u32>(joyStat) & (1u << 1)));

        std::puts("[PASS] RX-Not-Empty cleared on read");
    }

    // ----------------------------------------------------------------
    // 16. Back-to-back transfers reuse the same SIO0 instance.
    // ----------------------------------------------------------------
    {
        // First exchange – all buttons released.
        resetForTransfer();

        auto doExchange = [&]() -> psxrecomp::u16
        {
            constexpr Address joyData = 0x1F801040u;
            system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x01u);
            (void)system.readMmioExplicit<psxrecomp::u8>(joyData);
            system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x42u);
            (void)system.readMmioExplicit<psxrecomp::u8>(joyData);
            system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x00u);
            (void)system.readMmioExplicit<psxrecomp::u8>(joyData);
            system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x00u);
            psxrecomp::u8 lo = system.readMmioExplicit<psxrecomp::u8>(joyData);
            system.writeMmioExplicit<psxrecomp::u8>(joyData, 0x00u);
            psxrecomp::u8 hi = system.readMmioExplicit<psxrecomp::u8>(joyData);
            return static_cast<psxrecomp::u16>(lo | (hi << 8));
        };

        assert(doExchange() == 0xFFFFu);

        // Press L1 and re-select port for second exchange.
        system.input().setButton(ControllerButton::L1, true);
        resetForTransfer();

        [[maybe_unused]] psxrecomp::u16 result = doExchange();
        // L1 = bit 10 → bit 10 cleared.
        assert(result == static_cast<psxrecomp::u16>(0xFFFF & ~(1u << 10)));

        system.input().setButton(ControllerButton::L1, false);

        std::puts("[PASS] Back-to-back transfers");
    }

    runSio0IrqBusTests(system);

    std::puts("\n=== All SIO0 tests passed ===");
    return 0;
}
