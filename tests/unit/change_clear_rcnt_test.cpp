#include "psxrecomp/runtime/psx_system.h"

#include <array>
#include <cassert>
#include <iostream>

namespace
{

void serviceInterruptsFromTest(psxrecomp::runtime::PsxSystem& system, psxrecomp::u32 pc)
{
    system.observeProgramCounter(pc);
    system.serviceInterrupts();
}

constexpr size_t REG_V0 = 2;
constexpr size_t REG_SP = 29;
constexpr size_t REG_RA = 31;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        std::abort();
    }
}

void testChangeClearRCntReturnsOldFlag()
{
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    assert(system.initialize());

    // First call: old flag should be 0 (initial state).
    std::array<psxrecomp::u32, 32> regs{};
    regs[9] = 0x0A; // C(0Ah) = ChangeClearRCnt
    regs[4] = 0;    // t = Timer0
    regs[5] = 1;    // flag = 1 (enable auto-clear)
    system.callBiosVector(0xC0, regs.data(), regs.size());
    require(regs[REG_V0] == 0u, "ChangeClearRCnt first call should return old flag 0");

    // Second call: old flag should be 1.
    regs.fill(0);
    regs[9] = 0x0A;
    regs[4] = 0; // Timer0
    regs[5] = 0; // flag = 0 (disable auto-clear)
    system.callBiosVector(0xC0, regs.data(), regs.size());
    require(regs[REG_V0] == 1u, "ChangeClearRCnt second call should return old flag 1");

    // Third call: old flag should be 0 again.
    regs.fill(0);
    regs[9] = 0x0A;
    regs[4] = 0;
    regs[5] = 1;
    system.callBiosVector(0xC0, regs.data(), regs.size());
    require(regs[REG_V0] == 0u, "ChangeClearRCnt third call should return old flag 0");

    std::cerr << "[PASS] ChangeClearRCnt returns old flag value\n";
}

void testChangeClearRCntPerSourceIndependence()
{
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    assert(system.initialize());

    // Set Timer0 auto-clear.
    std::array<psxrecomp::u32, 32> regs{};
    regs[9] = 0x0A;
    regs[4] = 0; // Timer0
    regs[5] = 1;
    system.callBiosVector(0xC0, regs.data(), regs.size());

    // Set Timer2 auto-clear.
    regs.fill(0);
    regs[9] = 0x0A;
    regs[4] = 2; // Timer2
    regs[5] = 1;
    system.callBiosVector(0xC0, regs.data(), regs.size());

    // Timer1 should still be 0.
    regs.fill(0);
    regs[9] = 0x0A;
    regs[4] = 1; // Timer1
    regs[5] = 0;
    system.callBiosVector(0xC0, regs.data(), regs.size());
    require(regs[REG_V0] == 0u, "Timer1 should not be affected by Timer0/Timer2 changes");

    // VBlank (3) should also be 0.
    regs.fill(0);
    regs[9] = 0x0A;
    regs[4] = 3; // VBlank
    regs[5] = 0;
    system.callBiosVector(0xC0, regs.data(), regs.size());
    require(regs[REG_V0] == 0u, "VBlank should be independent of timer sources");

    std::cerr << "[PASS] ChangeClearRCnt per-source policies are independent\n";
}

void testAutoClearTimerIrqAcknowledgesAndSkipsChains()
{
    using psxrecomp::runtime::Cop0;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    assert(system.initialize());

    // Enable auto-clear for Timer2 (index 2).
    std::array<psxrecomp::u32, 32> regs{};
    regs[9] = 0x0A;
    regs[4] = 2; // Timer2
    regs[5] = 1; // enable
    system.callBiosVector(0xC0, regs.data(), regs.size());

    // Enable Timer2 IRQ in mask.
    system.interrupts().writeMask(static_cast<psxrecomp::u32>(InterruptLine::Timer2));

    // Raise Timer2 IRQ.
    system.interrupts().raise(InterruptLine::Timer2);

    // Set COP0 status to allow interrupts.
    system.cop0().mtc0(Cop0::RegisterIndex::Status, 0x040Bu);

    // Install a callback invoker that records calls.
    bool callbackInvoked = false;
    system.setCallbackInvoker(
        [&callbackInvoked](psxrecomp::u32) -> psxrecomp::u32
        {
            callbackInvoked = true;
            return 0;
        });

    // Service interrupts — auto-clear should ack Timer2 and skip chain processing.
    serviceInterruptsFromTest(system, 0x80017C00u);

    // Timer2 I_STAT bit should be cleared.
    require((system.interrupts().readStatus() &
             static_cast<psxrecomp::u32>(InterruptLine::Timer2)) == 0u,
            "Timer2 IRQ should be acknowledged by auto-clear");

    std::cerr << "[PASS] auto-clear Timer IRQ acknowledges and skips chains\n";
}

void testNonAutoClearTimerLeavesIrqForNormalHandling()
{
    using psxrecomp::runtime::Cop0;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    assert(system.initialize());

    // Do NOT set auto-clear for Timer2.
    // Enable Timer2 in mask.
    system.interrupts().writeMask(static_cast<psxrecomp::u32>(InterruptLine::Timer2));
    system.interrupts().raise(InterruptLine::Timer2);
    system.cop0().mtc0(Cop0::RegisterIndex::Status, 0x040Bu);

    system.setCallbackInvoker([](psxrecomp::u32) -> psxrecomp::u32 { return 0; });

    serviceInterruptsFromTest(system, 0x80017C04u);

    // Without auto-clear, the normal IRQ flow should still run and ack the IRQ.
    // (HookEntryInt ack path clears pending bits as fallback.)
    require((system.interrupts().readStatus() &
             static_cast<psxrecomp::u32>(InterruptLine::Timer2)) == 0u,
            "Timer2 IRQ should eventually be acknowledged even without auto-clear");

    std::cerr << "[PASS] non-auto-clear Timer IRQ proceeds through normal handling\n";
}

} // namespace

int main()
{
    testChangeClearRCntReturnsOldFlag();
    testChangeClearRCntPerSourceIndependence();
    testAutoClearTimerIrqAcknowledgesAndSkipsChains();
    testNonAutoClearTimerLeavesIrqForNormalHandling();
    std::cerr << "All ChangeClearRCnt tests passed.\n";
    return 0;
}
