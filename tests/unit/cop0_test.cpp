#include "psxrecomp/runtime/cop0.h"

#include <cassert>

int main()
{
    using psxrecomp::runtime::Cop0;
    constexpr psxrecomp::u32 CAUSE_IP0_IP1_MASK = 0x00000300u;
    constexpr psxrecomp::u32 CAUSE_IP2_BIT = 1u << 10;
    constexpr psxrecomp::u32 STATUS_IEC_BIT = 1u << 0;
    constexpr psxrecomp::u32 STATUS_IM0_BIT = 1u << 8;
    constexpr psxrecomp::u32 STATUS_IM2_BIT = 1u << 10;
    constexpr psxrecomp::u32 STATUS_CU2_BIT = 1u << 30;

    Cop0 cop0;
    cop0.reset();

    // Seed IEc/KUc/IEp/KUp/IEo/KUo as 00_1011.
    cop0.mtc0(Cop0::RegisterIndex::Status, 0x0Bu);
    cop0.exceptionEnter(Cop0::ExceptionCode::Interrupt, 0x80012340u, false);

    assert((cop0.mfc0(Cop0::RegisterIndex::Status) & 0x3Fu) == 0x2Cu);
    assert((cop0.mfc0(Cop0::RegisterIndex::Cause) & 0x7Cu) == 0x00u);
    assert(cop0.mfc0(Cop0::RegisterIndex::Epc) == 0x80012340u);

    cop0.rfe();
    assert((cop0.mfc0(Cop0::RegisterIndex::Status) & 0x3Fu) == 0x0Bu);

    // Delay-slot exception should set BD and store EPC=pc-4.
    cop0.exceptionEnter(Cop0::ExceptionCode::Breakpoint, 0x80012344u, true, 0x0000FFFCu);
    assert((cop0.mfc0(Cop0::RegisterIndex::Cause) & 0x7Cu) == (9u << 2));
    assert((cop0.mfc0(Cop0::RegisterIndex::Cause) & 0x80000000u) != 0u);
    assert(cop0.mfc0(Cop0::RegisterIndex::Epc) == 0x80012340u);
    assert(cop0.mfc0(Cop0::RegisterIndex::BadVAddr) == 0x0000FFFCu);

    // PRID should expose a fixed PSX-ish ID and ignore writes.
    cop0.mtc0(Cop0::RegisterIndex::PrId, 0xDEADBEEFu);
    assert(cop0.mfc0(Cop0::RegisterIndex::PrId) == 0x00000002u);

    // COP2 access is gated by Status.CU2.
    assert(!cop0.cop2Enabled());
    cop0.mtc0(Cop0::RegisterIndex::Status, STATUS_CU2_BIT);
    assert(cop0.cop2Enabled());
    cop0.mtc0(Cop0::RegisterIndex::Status, 0u);
    assert(!cop0.cop2Enabled());

    // Hardware-pending IP bit should be controlled by runtime wiring.
    cop0.setHardwareInterruptPending(true);
    assert((cop0.mfc0(Cop0::RegisterIndex::Cause) & CAUSE_IP2_BIT) != 0u);
    cop0.setHardwareInterruptPending(false);
    assert((cop0.mfc0(Cop0::RegisterIndex::Cause) & CAUSE_IP2_BIT) == 0u);

    // MTC0 Cause may only modify software-pending bits (IP0/IP1).
    cop0.setHardwareInterruptPending(true);
    const psxrecomp::u32 causeBefore = cop0.mfc0(Cop0::RegisterIndex::Cause);
    cop0.mtc0(Cop0::RegisterIndex::Cause, 0xFFFFFFFFu);
    const psxrecomp::u32 causeAfter = cop0.mfc0(Cop0::RegisterIndex::Cause);
    assert((causeAfter & CAUSE_IP0_IP1_MASK) == CAUSE_IP0_IP1_MASK);
    assert((causeAfter & CAUSE_IP2_BIT) != 0u);
    assert((causeAfter & 0xFFFFFCFFu) == (causeBefore & 0xFFFFFCFFu));

    // IRQ-take gating uses IEc + (Status.IM & Cause.IP) and exception mode.
    cop0.mtc0(Cop0::RegisterIndex::Status, STATUS_IEC_BIT | STATUS_IM2_BIT);
    cop0.mtc0(Cop0::RegisterIndex::Cause, 0u);
    cop0.setHardwareInterruptPending(true);
    assert(cop0.shouldTakeInterruptException());
    cop0.exceptionEnter(Cop0::ExceptionCode::Interrupt, 0x80001000u, false);
    assert(cop0.isInExceptionMode());
    assert(!cop0.shouldTakeInterruptException());
    cop0.rfe();
    assert(!cop0.isInExceptionMode());
    assert(cop0.shouldTakeInterruptException());
    cop0.setHardwareInterruptPending(false);
    assert(!cop0.shouldTakeInterruptException());

    // Software pending bits (Cause.IP0/IP1) should also trigger interrupts
    // when the corresponding Status.IM bit and IEc are enabled.
    cop0.mtc0(Cop0::RegisterIndex::Status, STATUS_IEC_BIT | STATUS_IM0_BIT);
    cop0.mtc0(Cop0::RegisterIndex::Cause, 1u << 8);
    assert(cop0.shouldTakeInterruptException());
    cop0.mtc0(Cop0::RegisterIndex::Cause, 0u);
    assert(!cop0.shouldTakeInterruptException());

    return 0;
}
