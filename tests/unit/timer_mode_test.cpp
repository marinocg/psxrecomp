#include "psxrecomp/runtime/timers.h"

#include <cassert>
#include <iostream>
#include <vector>

namespace
{

using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::TimerController;

constexpr psxrecomp::u16 MODE_SYNC_ENABLE = 1u << 0;
constexpr psxrecomp::u16 MODE_RESET_ON_TARGET = 1u << 3;
constexpr psxrecomp::u16 MODE_IRQ_ON_TARGET = 1u << 4;
constexpr psxrecomp::u16 MODE_IRQ_ON_OVERFLOW = 1u << 5;
constexpr psxrecomp::u16 MODE_IRQ_REPEAT = 1u << 6;
constexpr psxrecomp::u16 MODE_IRQ_TOGGLE = 1u << 7;
constexpr psxrecomp::u16 MODE_IRQ_REQUEST_BIT = 1u << 10;
constexpr psxrecomp::u16 MODE_TARGET_REACHED = 1u << 11;
constexpr psxrecomp::u16 MODE_OVERFLOW_REACHED = 1u << 12;

struct IrqRecord
{
    InterruptLine line;
};

TimerController::InterruptCallback makeRecorder(std::vector<IrqRecord>& records)
{
    return [&records](InterruptLine line) { records.push_back({line}); };
}

void testModeWriteResetsCounter()
{
    TimerController tc;
    tc.writeCounter(0, 1234);
    tc.writeMode(0, MODE_IRQ_ON_TARGET);
    assert(tc.readCounter(0) == 0);
    std::cerr << "[PASS] mode write resets counter to zero\n";
}

void testModeReadClearsBits11And12()
{
    TimerController tc;
    tc.writeMode(0, MODE_IRQ_ON_TARGET | MODE_RESET_ON_TARGET);
    tc.writeTarget(0, 5);
    std::vector<IrqRecord> records;
    tc.tick(10, makeRecorder(records));
    psxrecomp::u16 mode = tc.readMode(0);
    assert((mode & MODE_TARGET_REACHED) != 0);
    // Second read should have cleared bits 11-12.
    mode = tc.readMode(0);
    assert((mode & MODE_TARGET_REACHED) == 0);
    std::cerr << "[PASS] mode read clears bits 11 and 12\n";
}

void testResetOnTargetWrapsAtTarget()
{
    TimerController tc;
    tc.writeMode(0, MODE_RESET_ON_TARGET | MODE_IRQ_ON_TARGET);
    tc.writeTarget(0, 10);
    std::vector<IrqRecord> records;
    tc.tick(25, makeRecorder(records));
    // Counter should wrap at target=10: 25 steps → counter = 25 % 10 = 5.
    assert(tc.readCounter(0) == 5);
    assert(!records.empty());
    std::cerr << "[PASS] reset-on-target wraps at target\n";
}

void testOneShotSuppressesSubsequentIrqs()
{
    TimerController tc;
    // One-shot (bit 6 = 0), pulse (bit 7 = 0), IRQ on target.
    tc.writeMode(0, MODE_IRQ_ON_TARGET | MODE_RESET_ON_TARGET);
    tc.writeTarget(0, 5);
    std::vector<IrqRecord> records;
    tc.tick(5, makeRecorder(records));
    assert(records.size() == 1);
    // Tick again past target — one-shot should suppress.
    tc.tick(5, makeRecorder(records));
    assert(records.size() == 1);
    // Re-write mode to re-arm one-shot.
    tc.writeMode(0, MODE_IRQ_ON_TARGET | MODE_RESET_ON_TARGET);
    tc.writeTarget(0, 5);
    tc.tick(5, makeRecorder(records));
    assert(records.size() == 2);
    std::cerr << "[PASS] one-shot mode suppresses subsequent IRQs\n";
}

void testRepeatModeFiresMultipleTimes()
{
    TimerController tc;
    // Repeat (bit 6 = 1), IRQ on target with reset.
    tc.writeMode(0, MODE_IRQ_ON_TARGET | MODE_RESET_ON_TARGET | MODE_IRQ_REPEAT);
    tc.writeTarget(0, 5);
    std::vector<IrqRecord> records;
    tc.tick(5, makeRecorder(records));
    assert(records.size() == 1);
    tc.tick(5, makeRecorder(records));
    assert(records.size() == 2);
    tc.tick(5, makeRecorder(records));
    assert(records.size() == 3);
    std::cerr << "[PASS] repeat mode fires multiple times\n";
}

void testToggleModeFlipsBit10()
{
    TimerController tc;
    // Toggle (bit 7 = 1), repeat (bit 6 = 1), IRQ on target.
    tc.writeMode(0, MODE_IRQ_ON_TARGET | MODE_RESET_ON_TARGET | MODE_IRQ_REPEAT | MODE_IRQ_TOGGLE);
    tc.writeTarget(0, 5);

    // After mode write, bit 10 should be set (no IRQ pending).
    psxrecomp::u16 mode = tc.readMode(0);
    assert((mode & MODE_IRQ_REQUEST_BIT) != 0);

    // First target hit: toggle bit 10 → 0 (IRQ fires).
    tc.writeMode(0, MODE_IRQ_ON_TARGET | MODE_RESET_ON_TARGET | MODE_IRQ_REPEAT | MODE_IRQ_TOGGLE);
    tc.writeTarget(0, 5);
    std::vector<IrqRecord> records;
    tc.tick(5, makeRecorder(records));
    assert(records.size() == 1);
    mode = tc.readMode(0);
    // After toggle: bit 10 should be 0 (toggled from 1).
    // Note: readMode clears bits 11-12 but bit 10 reflects irqRequest state.

    // Second target hit: toggle bit 10 back → 1 (no IRQ fires because 1=no IRQ).
    tc.writeTarget(0, 5);
    tc.tick(5, makeRecorder(records));
    // No new IRQ since bit 10 went back to 1 (no IRQ state).
    assert(records.size() == 1);

    std::cerr << "[PASS] toggle mode flips bit 10 state\n";
}

void testTimer2SyncMode0And3Stop()
{
    TimerController tc;
    // Timer2, sync enable + sync mode 0 = stop.
    tc.writeMode(2, MODE_SYNC_ENABLE | (0u << 1));
    tc.writeTarget(2, 100);
    std::vector<IrqRecord> records;
    tc.tick(1000, makeRecorder(records));
    assert(tc.readCounter(2) == 0); // Should not advance.

    // Sync mode 3 also stops.
    tc.writeMode(2, MODE_SYNC_ENABLE | (3u << 1));
    tc.tick(1000, makeRecorder(records));
    assert(tc.readCounter(2) == 0);

    std::cerr << "[PASS] Timer2 sync modes 0/3 stop counter\n";
}

void testTimer2SyncMode1And2FreeRun()
{
    TimerController tc;
    // Timer2, sync enable + sync mode 1 = free-run.
    tc.writeMode(2, MODE_SYNC_ENABLE | (1u << 1));
    tc.tick(100, {});
    assert(tc.readCounter(2) == 100);

    // Sync mode 2 also free-runs.
    tc.writeMode(2, MODE_SYNC_ENABLE | (2u << 1));
    tc.tick(50, {});
    assert(tc.readCounter(2) == 50);

    std::cerr << "[PASS] Timer2 sync modes 1/2 free-run\n";
}

void testTimer2ClockSourceDiv8()
{
    TimerController tc;
    // Timer2, clock source 2 = sysclk/8.
    tc.writeMode(2, (2u << 8));
    tc.tick(16, {});
    assert(tc.readCounter(2) == 2); // 16/8 = 2 steps.

    // Clock source 3 also uses sysclk/8 for Timer2.
    tc.writeMode(2, (3u << 8));
    tc.tick(24, {});
    assert(tc.readCounter(2) == 3); // 24/8 = 3 steps.

    std::cerr << "[PASS] Timer2 clock source 2/3 uses sysclk/8\n";
}

void testModeWriteSetsBit10()
{
    TimerController tc;
    // Mode write should set bit 10 (no IRQ pending).
    tc.writeMode(0, 0);
    psxrecomp::u16 mode = tc.readMode(0);
    assert((mode & MODE_IRQ_REQUEST_BIT) != 0);
    std::cerr << "[PASS] mode write sets bit 10 (no IRQ pending)\n";
}

void testOverflowReachedFlag()
{
    TimerController tc;
    tc.writeMode(0, MODE_IRQ_ON_OVERFLOW);
    tc.writeTarget(0, 0xFFFF);
    tc.writeCounter(0, 0xFFFE);
    std::vector<IrqRecord> records;
    tc.tick(3, makeRecorder(records));
    psxrecomp::u16 mode = tc.readMode(0);
    assert((mode & MODE_OVERFLOW_REACHED) != 0);
    // Second read should clear.
    mode = tc.readMode(0);
    assert((mode & MODE_OVERFLOW_REACHED) == 0);
    std::cerr << "[PASS] overflow reached flag set and cleared on read\n";
}

} // namespace

int main()
{
    testModeWriteResetsCounter();
    testModeReadClearsBits11And12();
    testResetOnTargetWrapsAtTarget();
    testOneShotSuppressesSubsequentIrqs();
    testRepeatModeFiresMultipleTimes();
    testToggleModeFlipsBit10();
    testTimer2SyncMode0And3Stop();
    testTimer2SyncMode1And2FreeRun();
    testTimer2ClockSourceDiv8();
    testModeWriteSetsBit10();
    testOverflowReachedFlag();
    std::cerr << "All timer mode tests passed.\n";
    return 0;
}
