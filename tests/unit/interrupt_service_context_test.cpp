#include "psxrecomp/runtime/psx_system.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace
{
constexpr size_t REG_V0 = 2;
constexpr size_t REG_S0 = 16;
constexpr size_t REG_GP = 28;
constexpr size_t REG_SP = 29;
constexpr size_t REG_FP = 30;
constexpr size_t REG_RA = 31;

struct CallbackContext
{
    std::array<psxrecomp::u32, 32> regs{};
    psxrecomp::u32 hi = 0;
    psxrecomp::u32 lo = 0;
};

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << "\n";
        std::abort();
    }
}

void installSyntheticCallbackBridge(psxrecomp::runtime::PsxSystem& system, CallbackContext& context,
                                    psxrecomp::u32 resumeAddress)
{
    using psxrecomp::runtime::PsxSystem;

    system.setCallbackInvoker(
        [&system, &context, resumeAddress](psxrecomp::u32 address) -> psxrecomp::u32
        {
            const auto savedRegs = context.regs;
            const psxrecomp::u32 savedHi = context.hi;
            const psxrecomp::u32 savedLo = context.lo;
            const psxrecomp::u32 callbackCommitGeneration =
                system.callbackContextCommitGeneration();
            const auto callbackContextDisposition =
                system.consumePendingCallbackRegisters(context.regs);
            const auto shouldCommitCallbackContext =
                [&system, callbackCommitGeneration, callbackContextDisposition]()
            {
                return callbackContextDisposition ==
                           PsxSystem::CallbackContextDisposition::CommitMutated ||
                       system.callbackContextCommitGeneration() != callbackCommitGeneration;
            };

            try
            {
                if (address == resumeAddress)
                {
                    context.regs[REG_V0] = 0u;
                    context.regs[REG_S0] = 0x55667788u;
                    context.hi = 0xCAFEBABEu;
                    context.lo = 0xFACE1234u;
                    system.write<psxrecomp::u32>(0x80015000u, 0x12345678u);

                    std::array<psxrecomp::u32, 32> biosRegs{};
                    biosRegs[9] = 0x17; // B0:ReturnFromException
                    system.callBiosVector(0xB0, biosRegs.data(), biosRegs.size());
                }

                const psxrecomp::u32 callbackResult = context.regs[REG_V0];
                if (!shouldCommitCallbackContext())
                {
                    context.regs = savedRegs;
                    context.hi = savedHi;
                    context.lo = savedLo;
                }
                return callbackResult;
            }
            catch (...)
            {
                if (!shouldCommitCallbackContext())
                {
                    context.regs = savedRegs;
                    context.hi = savedHi;
                    context.lo = savedLo;
                }
                throw;
            }
        });
}

void installHookEntryIntDescriptor(psxrecomp::runtime::PsxSystem& system,
                                   psxrecomp::u32 descriptorAddress, psxrecomp::u32 resumeAddress)
{
    std::array<psxrecomp::u32, 32> regs{};
    regs[9] = 0x13;              // A0:setjmp
    regs[4] = descriptorAddress; // jmp_buf
    regs[REG_RA] = resumeAddress;
    regs[REG_SP] = 0x8001F200u;
    regs[REG_FP] = 0x8001F220u;
    regs[REG_GP] = 0x80011000u;
    regs[REG_S0] = 0x11223344u;
    system.callBiosVector(0xA0, regs.data(), regs.size());
    assert(regs[REG_V0] == 0u);

    regs.fill(0u);
    regs[9] = 0x19;              // B0:HookEntryInt
    regs[4] = descriptorAddress; // descriptor
    system.callBiosVector(0xB0, regs.data(), regs.size());
    assert(regs[REG_V0] == 0u);
}

void serviceInterruptsLikeGenerated(psxrecomp::runtime::PsxSystem& system, CallbackContext& context)
{
    const auto interruptSavedRegs = context.regs;
    const psxrecomp::u32 interruptSavedHi = context.hi;
    const psxrecomp::u32 interruptSavedLo = context.lo;
    const psxrecomp::u32 interruptCallbackCommitGeneration =
        system.callbackContextCommitGeneration();

    system.serviceInterrupts();

    if (system.callbackContextCommitGeneration() != interruptCallbackCommitGeneration)
    {
        context.regs = interruptSavedRegs;
        context.hi = interruptSavedHi;
        context.lo = interruptSavedLo;
    }
}

void triggerVblank(psxrecomp::runtime::PsxSystem& system)
{
    using psxrecomp::runtime::Cop0;
    using psxrecomp::runtime::InterruptLine;

    system.interrupts().writeMask(static_cast<psxrecomp::u32>(InterruptLine::VBlank));
    system.cop0().mtc0(Cop0::RegisterIndex::Status, 0x040Bu);
    system.interrupts().raise(InterruptLine::VBlank);
}

void testHookEntryIntDoesNotLeakIntoInterruptedContext()
{
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    assert(system.initialize());

    constexpr psxrecomp::u32 descriptorAddress = 0x80014000u;
    constexpr psxrecomp::u32 resumeAddress = 0x80012340u;
    installHookEntryIntDescriptor(system, descriptorAddress, resumeAddress);

    CallbackContext context;
    context.regs.fill(0u);
    context.regs[REG_V0] = 1u;
    context.regs[REG_S0] = 0xAABBCCDDu;
    context.regs[REG_RA] = 0x8001021Cu;
    context.regs[REG_SP] = 0x8001FFE0u;
    context.hi = 0x13572468u;
    context.lo = 0x24681357u;

    installSyntheticCallbackBridge(system, context, resumeAddress);
    triggerVblank(system);
    serviceInterruptsLikeGenerated(system, context);

    require(system.read<psxrecomp::u32>(0x80015000u) == 0x12345678u,
            "HookEntryInt callback did not execute its memory side effects");
    require(context.regs[REG_V0] == 1u,
            "HookEntryInt callback leaked resumed v0 into interrupted context");
    require(context.regs[REG_S0] == 0xAABBCCDDu,
            "HookEntryInt callback leaked resumed S0 into interrupted context");
    require(context.regs[REG_RA] == 0x8001021Cu,
            "HookEntryInt callback leaked resumed RA into interrupted context");
    require(context.hi == 0x13572468u,
            "HookEntryInt callback leaked resumed HI into interrupted context");
    require(context.lo == 0x24681357u,
            "HookEntryInt callback leaked resumed LO into interrupted context");
    require((system.interrupts().readStatus() &
             static_cast<psxrecomp::u32>(InterruptLine::VBlank)) == 0u,
            "VBlank interrupt was not acknowledged");

    std::cerr << "[PASS] generated interrupt service restores interrupted register context\n";
}
} // namespace

int main()
{
    testHookEntryIntDoesNotLeakIntoInterruptedContext();
    std::cerr << "All interrupt service context tests passed.\n";
    return 0;
}
