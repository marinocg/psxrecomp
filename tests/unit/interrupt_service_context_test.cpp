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
                    context.regs[3] = 0x10203040u;  // v1
                    context.regs[4] = 0x11112222u;  // a0
                    context.regs[5] = 0x33334444u;  // a1
                    context.regs[6] = 0x55556666u;  // a2
                    context.regs[9] = 0x77778888u;  // t1
                    context.regs[10] = 0x9999AAAAu; // t2
                    context.regs[REG_S0] = 0x55667788u;
                    context.regs[17] = 0xBBBBCCCCu; // s1
                    context.regs[18] = 0xDDDDEEEEu; // s2
                    context.regs[19] = 0xF0F0F0F0u; // s3
                    context.regs[20] = 0x12344321u; // s4
                    context.regs[REG_RA] = 0x8001ABCDu;
                    context.hi = 0xCAFEBABEu;
                    context.lo = 0xFACE1234u;
                    system.write<psxrecomp::u32>(0x80015000u, 0x12345678u);

                    // Acknowledge VBlank: PSX-SPX — the handler owns the I_STAT ack.
                    system.interrupts().writeStatus(
                        ~static_cast<psxrecomp::u32>(psxrecomp::runtime::InterruptLine::VBlank));

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

    system.observeProgramCounter(0x80014100u);
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

    system.observeProgramCounter(0x80014104u);
    system.interrupts().writeMask(static_cast<psxrecomp::u32>(InterruptLine::VBlank));
    system.cop0().mtc0(Cop0::RegisterIndex::Status, 0x040Bu);
    system.interrupts().raise(InterruptLine::VBlank);
}

void testHookEntryIntDoesNotLeakIntoInterruptedContext()
{
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    require(system.initialize(), "Failed to initialize system");

    constexpr psxrecomp::u32 descriptorAddress = 0x80014000u;
    constexpr psxrecomp::u32 resumeAddress = 0x80012340u;
    installHookEntryIntDescriptor(system, descriptorAddress, resumeAddress);

    CallbackContext context;
    context.regs.fill(0u);
    context.regs[REG_V0] = 1u;
    context.regs[REG_S0] = 0xAABBCCDDu;
    context.regs[3] = 0x01010101u;  // v1
    context.regs[4] = 0x02020202u;  // a0
    context.regs[5] = 0x03030303u;  // a1
    context.regs[6] = 0x04040404u;  // a2
    context.regs[9] = 0x05050505u;  // t1
    context.regs[10] = 0x06060606u; // t2
    context.regs[17] = 0x07070707u; // s1
    context.regs[18] = 0x08080808u; // s2
    context.regs[19] = 0x09090909u; // s3
    context.regs[20] = 0x0A0A0A0Au; // s4
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
    require(context.regs[3] == 0x01010101u,
            "HookEntryInt callback leaked resumed v1 into interrupted context");
    require(context.regs[4] == 0x02020202u,
            "HookEntryInt callback leaked resumed a0 into interrupted context");
    require(context.regs[5] == 0x03030303u,
            "HookEntryInt callback leaked resumed a1 into interrupted context");
    require(context.regs[6] == 0x04040404u,
            "HookEntryInt callback leaked resumed a2 into interrupted context");
    require(context.regs[9] == 0x05050505u,
            "HookEntryInt callback leaked resumed t1 into interrupted context");
    require(context.regs[10] == 0x06060606u,
            "HookEntryInt callback leaked resumed t2 into interrupted context");
    require(context.regs[17] == 0x07070707u,
            "HookEntryInt callback leaked resumed s1 into interrupted context");
    require(context.regs[18] == 0x08080808u,
            "HookEntryInt callback leaked resumed s2 into interrupted context");
    require(context.regs[19] == 0x09090909u,
            "HookEntryInt callback leaked resumed s3 into interrupted context");
    require(context.regs[20] == 0x0A0A0A0Au,
            "HookEntryInt callback leaked resumed s4 into interrupted context");
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
