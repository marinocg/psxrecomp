#include "psxrecomp/runtime/cop0.h"

#include <cassert>

int main()
{
    using psxrecomp::runtime::Cop0;

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

    // Unimplemented registers should read as zero and ignore writes.
    cop0.mtc0(15, 0xDEADBEEFu);
    assert(cop0.mfc0(15) == 0u);

    return 0;
}
