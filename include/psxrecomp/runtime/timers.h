#pragma once

#include "psxrecomp/runtime/interrupt_controller.h"
#include "psxrecomp/types.h"

#include <array>
#include <functional>

namespace psxrecomp
{
namespace runtime
{

/// PSX-SPX compliant root counter (timer) controller.
///
/// Mode register bits (per PSX-SPX):
///   0: Sync enable (0=free-run, 1=use sync mode in bits 1-2)
///   1-2: Sync mode (timer-specific meanings)
///   3: Reset on target (0=wrap at 0xFFFF, 1=reset at target)
///   4: IRQ on target reached
///   5: IRQ on overflow (counter wraps past 0xFFFF)
///   6: IRQ repeat (0=one-shot, 1=repeat)
///   7: IRQ toggle (0=pulse/bit10 momentarily set, 1=toggle bit10)
///   8-9: Clock source (timer-specific)
///   10: Interrupt request flag (0=IRQ requested, 1=no IRQ) — set on reset, toggled/pulsed by IRQ
///   11: Target reached flag (set by HW, cleared on read)
///   12: Overflow reached flag (set by HW, cleared on read)
class TimerController
{
  public:
    using InterruptCallback = std::function<void(InterruptLine)>;

    void reset();

    u16 readCounter(size_t index) const;
    u16 readMode(size_t index);
    u16 readTarget(size_t index) const;

    void writeCounter(size_t index, u16 value);
    void writeMode(size_t index, u16 value);
    void writeTarget(size_t index, u16 value);

    void tick(u32 cpuCycles, const InterruptCallback& onInterrupt);
    void tickDisplayLine(const InterruptCallback& onInterrupt);

  private:
    struct Channel
    {
        u16 counter = 0;
        u16 mode = 0;
        u16 target = 0;
        u32 cycleCarry = 0;
        bool targetReached = false;
        bool overflowReached = false;
        bool irqRequest = true;    ///< Bit 10 state: true=no IRQ pending (active LOW on HW).
        bool oneShotFired = false; ///< True if one-shot IRQ already fired since last mode write.
    };

    std::array<Channel, 3> m_channels = {};

    static void advanceChannel(Channel& channel, size_t index, u32 steps,
                               const InterruptCallback& onInterrupt);
    static void tickChannel(Channel& channel, size_t index, u32 cpuCycles,
                            const InterruptCallback& onInterrupt);
    static bool isValidIndex(size_t index);
    static u32 dividerForChannel(size_t index, const Channel& channel);
    static bool usesDisplayLineClock(size_t index, const Channel& channel);
    static bool isStopped(size_t index, const Channel& channel);
    static InterruptLine interruptLineForTimer(size_t index);

    /// Handle IRQ request state transitions (pulse/toggle, one-shot/repeat).
    static void processIrqRequest(Channel& channel, const InterruptCallback& onInterrupt,
                                  InterruptLine line);
};

} // namespace runtime
} // namespace psxrecomp
