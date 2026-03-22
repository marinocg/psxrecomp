#include "psxrecomp/runtime/psx_system.h"

#include <array>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
constexpr size_t REG_V0 = 2;
constexpr size_t REG_S0 = 16;
constexpr size_t REG_S1 = 17;
constexpr size_t REG_S7 = 23;
constexpr size_t REG_GP = 28;
constexpr size_t REG_SP = 29;
constexpr size_t REG_FP = 30;
constexpr size_t REG_RA = 31;
constexpr psxrecomp::u32 AllocatorScanPointerGlobal = 0x800577BCu;
constexpr psxrecomp::u32 AllocatorBackupPointerGlobal = 0x80057F38u;
constexpr psxrecomp::u32 HeapBlock0 = 0x80060000u;
constexpr psxrecomp::u32 HeapBlock1 = HeapBlock0 + 0x24u;
constexpr psxrecomp::u32 HeapSentinel = 0xFFFFFFFEu;

struct CallbackContext
{
    std::array<psxrecomp::u32, 32> regs{};
    psxrecomp::u32 hi = 0;
    psxrecomp::u32 lo = 0;
};

using CallbackBody =
    std::function<psxrecomp::u32(psxrecomp::u32, CallbackContext&, psxrecomp::runtime::PsxSystem&)>;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << "\n";
        std::abort();
    }
}

CallbackContext makeSeededContext()
{
    CallbackContext context;
    context.regs.fill(0xDEADBEEFu);
    context.regs[REG_V0] = 0xAAAAAAAAu;
    context.regs[REG_RA] = 0xBBBBBBBBu;
    context.regs[REG_SP] = 0xCCCCCCCCu;
    context.regs[REG_FP] = 0xDDDDDDDDu;
    context.regs[REG_GP] = 0xEEEEEEEEu;
    for (size_t index = 0; index < 8; ++index)
    {
        context.regs[REG_S0 + index] = 0x90000000u + static_cast<psxrecomp::u32>(index);
    }
    context.hi = 0x13572468u;
    context.lo = 0x24681357u;
    return context;
}

void installSyntheticCallbackHarness(psxrecomp::runtime::PsxSystem& system,
                                     CallbackContext& context, const CallbackBody& body)
{
    using psxrecomp::runtime::PsxSystem;

    system.setCallbackInvoker(
        [&context, &system, body](psxrecomp::u32 address) -> psxrecomp::u32
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
                const psxrecomp::u32 callbackResult = body(address, context, system);
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
                                   psxrecomp::u32 descriptorAddress, psxrecomp::u32 resumeAddress,
                                   psxrecomp::u32 savedSp, psxrecomp::u32 savedFp,
                                   psxrecomp::u32 savedGp,
                                   const std::array<psxrecomp::u32, 8>& savedS)
{
    std::array<psxrecomp::u32, 32> regs{};
    regs[9] = 0x13;              // A0:setjmp
    regs[4] = descriptorAddress; // jmp_buf
    regs[31] = resumeAddress;    // saved RA / resume target
    regs[29] = savedSp;
    regs[30] = savedFp;
    regs[28] = savedGp;
    for (size_t index = 0; index < savedS.size(); ++index)
    {
        regs[REG_S0 + index] = savedS[index];
    }
    system.callBiosVector(0xA0, regs.data(), regs.size());
    assert(regs[REG_V0] == 0u);

    regs.fill(0u);
    regs[9] = 0x19;              // B0:HookEntryInt
    regs[4] = descriptorAddress; // descriptor address
    system.callBiosVector(0xB0, regs.data(), regs.size());
    assert(regs[REG_V0] == 0u);
}

void triggerVblank(psxrecomp::runtime::PsxSystem& system)
{
    using psxrecomp::runtime::InterruptLine;
    system.observeProgramCounter(0x80014000u);
    system.interrupts().writeMask(static_cast<psxrecomp::u32>(InterruptLine::VBlank));
    system.cop0().mtc0(psxrecomp::runtime::Cop0::RegisterIndex::Status, 0x040Bu);
    system.interrupts().raise(InterruptLine::VBlank);
    system.serviceInterrupts();
}

void installValidAllocatorHeap(psxrecomp::runtime::PsxSystem& system)
{
    system.write<psxrecomp::u32>(AllocatorScanPointerGlobal, HeapBlock0);
    system.write<psxrecomp::u32>(AllocatorBackupPointerGlobal, HeapBlock0);
    system.write<psxrecomp::u32>(HeapBlock0, 0x20u);
    system.write<psxrecomp::u32>(HeapBlock1, HeapSentinel);
}

void testRegularCallbackReturnRestoresCallerRegs()
{
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    assert(system.initialize());

    CallbackContext context = makeSeededContext();
    const CallbackContext original = context;
    psxrecomp::runtime::PsxSystem::CallbackContextDisposition disposition =
        PsxSystem::CallbackContextDisposition::CommitMutated;

    installSyntheticCallbackHarness(system, context,
                                    [&disposition](psxrecomp::u32, CallbackContext& callbackContext,
                                                   PsxSystem& systemRef) -> psxrecomp::u32
                                    {
                                        disposition = systemRef.consumePendingCallbackRegisters(
                                            callbackContext.regs);
                                        callbackContext.regs[REG_V0] = 0x12345678u;
                                        callbackContext.regs[REG_S0] = 0x11112222u;
                                        callbackContext.hi = 0x33334444u;
                                        callbackContext.lo = 0x55556666u;
                                        return callbackContext.regs[REG_V0];
                                    });

    const psxrecomp::u32 result = system.invokeCallbackRaw(0x80012000u);
    if (result != 0x12345678u)
    {
        std::cerr << "regular callback returned wrong v0\n";
        std::abort();
    }
    require(disposition == PsxSystem::CallbackContextDisposition::RestoreSaved,
            "regular callback should restore saved caller context");
    for (size_t index = 0; index < original.regs.size(); ++index)
    {
        require(context.regs[index] == original.regs[index],
                "regular callback mutated caller registers");
    }
    require(context.hi == original.hi, "regular callback mutated caller HI");
    require(context.lo == original.lo, "regular callback mutated caller LO");

    std::cerr << "[PASS] regular callback return restores caller regs\n";
}

void testHookEntryIntResumeCommitsRestoredRegs()
{
    using psxrecomp::u32;
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    assert(system.initialize());

    constexpr u32 descriptorAddress = 0x80014000u;
    constexpr u32 resumeAddress = 0x80012340u;
    constexpr u32 savedSp = 0x8001FFE0u;
    constexpr u32 savedFp = 0x8001FFD0u;
    constexpr u32 savedGp = 0x80011000u;
    constexpr std::array<u32, 8> savedS = {
        0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u,
        0x55555555u, 0x66666666u, 0x77777777u, 0x88888888u,
    };

    CallbackContext context = makeSeededContext();
    installHookEntryIntDescriptor(system, descriptorAddress, resumeAddress, savedSp, savedFp,
                                  savedGp, savedS);

    installSyntheticCallbackHarness(
        system, context,
        [resumeAddress](u32 address, CallbackContext& callbackContext, PsxSystem& systemRef) -> u32
        {
            if (address == resumeAddress)
            {
                callbackContext.hi = 0xCAFEBABEu;
                callbackContext.lo = 0xFACE1234u;

                std::array<u32, 32> biosRegs{};
                biosRegs[9] = 0x17; // B0:ReturnFromException
                systemRef.callBiosVector(0xB0, biosRegs.data(), biosRegs.size());
            }
            return callbackContext.regs[REG_V0];
        });

    // This is the bug repro: before the fix, the restored HookEntryInt
    // context was lost when B0:17 unwound the callback body.
    triggerVblank(system);

    require(context.regs[REG_V0] == 1u, "HookEntryInt resume did not set v0=1");
    require(context.regs[REG_RA] == resumeAddress, "HookEntryInt resume lost RA");
    require(context.regs[REG_SP] == savedSp, "HookEntryInt resume lost SP");
    require(context.regs[REG_FP] == savedFp, "HookEntryInt resume lost FP");
    require(context.regs[REG_GP] == savedGp, "HookEntryInt resume lost GP");
    for (size_t index = 0; index < savedS.size(); ++index)
    {
        require(context.regs[REG_S0 + index] == savedS[index],
                "HookEntryInt resume lost saved S register");
    }
    require(context.hi == 0xCAFEBABEu, "HookEntryInt resume lost HI mutation");
    require(context.lo == 0xFACE1234u, "HookEntryInt resume lost LO mutation");

    std::cerr << "[PASS] HookEntryInt callback bridge commits resumed context\n";
}

void testConsumePendingCallbackRegistersAppliedExactlyOnce()
{
    using psxrecomp::u32;
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    assert(system.initialize());

    constexpr u32 descriptorAddress = 0x80014100u;
    constexpr u32 resumeAddress = 0x80012400u;
    constexpr std::array<u32, 8> savedS = {
        0xA0000000u, 0xA0000001u, 0xA0000002u, 0xA0000003u,
        0xA0000004u, 0xA0000005u, 0xA0000006u, 0xA0000007u,
    };
    CallbackContext context = makeSeededContext();
    installHookEntryIntDescriptor(system, descriptorAddress, resumeAddress, 0x8001F100u,
                                  0x8001F120u, 0x8001F140u, savedS);

    PsxSystem::CallbackContextDisposition firstDisposition =
        PsxSystem::CallbackContextDisposition::RestoreSaved;
    PsxSystem::CallbackContextDisposition secondDisposition =
        PsxSystem::CallbackContextDisposition::CommitMutated;
    PsxSystem::CallbackContextDisposition thirdDisposition =
        PsxSystem::CallbackContextDisposition::CommitMutated;

    system.setCallbackInvoker(
        [&system, &context, &firstDisposition, &secondDisposition](u32) -> u32
        {
            firstDisposition = system.consumePendingCallbackRegisters(context.regs);
            context.regs[REG_S0] = 0xABCDEF01u;
            secondDisposition = system.consumePendingCallbackRegisters(context.regs);
            context.regs[REG_S1] = 0x10203040u;
            return context.regs[REG_V0];
        });

    triggerVblank(system);
    require(firstDisposition == PsxSystem::CallbackContextDisposition::CommitMutated,
            "pending callback regs were not applied on first consume");
    require(secondDisposition == PsxSystem::CallbackContextDisposition::RestoreSaved,
            "pending callback regs applied more than once in same callback");
    require(context.regs[REG_V0] == 1u, "one-shot consume lost resumed v0");
    require(context.regs[REG_RA] == resumeAddress, "one-shot consume lost resumed RA");
    require(context.regs[REG_S0] == 0xABCDEF01u, "post-consume mutation of S0 was not kept");
    require(context.regs[REG_S1] == 0x10203040u, "post-consume mutation of S1 was not kept");

    system.setCallbackInvoker(
        [&system, &context, &thirdDisposition](u32) -> u32
        {
            thirdDisposition = system.consumePendingCallbackRegisters(context.regs);
            return 0;
        });
    (void)system.invokeCallbackRaw(0x80012500u);
    require(thirdDisposition == PsxSystem::CallbackContextDisposition::RestoreSaved,
            "pending callback regs leaked into later callback invocations");

    std::cerr << "[PASS] consumePendingCallbackRegisters applies exactly once\n";
}

void testPendingCallbackRegsSurviveExceptionResumePath()
{
    using psxrecomp::u32;
    using psxrecomp::runtime::PsxSystem;

    struct SyntheticResumeAbort : std::runtime_error
    {
        SyntheticResumeAbort() : std::runtime_error("synthetic resume abort") {}
    };

    PsxSystem system;
    assert(system.initialize());

    constexpr u32 descriptorAddress = 0x80014200u;
    constexpr u32 resumeAddress = 0x80012600u;
    constexpr u32 savedSp = 0x8001F200u;
    constexpr u32 savedFp = 0x8001F220u;
    constexpr u32 savedGp = 0x8001F240u;
    constexpr std::array<u32, 8> savedS = {
        0xB0000000u, 0xB0000001u, 0xB0000002u, 0xB0000003u,
        0xB0000004u, 0xB0000005u, 0xB0000006u, 0xB0000007u,
    };
    CallbackContext context = makeSeededContext();
    installHookEntryIntDescriptor(system, descriptorAddress, resumeAddress, savedSp, savedFp,
                                  savedGp, savedS);

    installSyntheticCallbackHarness(
        system, context,
        [resumeAddress](u32 address, CallbackContext& callbackContext, PsxSystem&) -> u32
        {
            if (address == resumeAddress)
            {
                callbackContext.hi = 0x0BADF00Du;
                callbackContext.lo = 0x00C0FFEEu;
                throw SyntheticResumeAbort{};
            }
            return 0;
        });

    bool threw = false;
    try
    {
        triggerVblank(system);
    }
    catch (const SyntheticResumeAbort&)
    {
        threw = true;
    }
    if (!threw)
    {
        std::cerr << "expected synthetic resume abort was not raised\n";
        std::abort();
    }
    require(context.regs[REG_V0] == 1u, "resume exception path lost v0");
    require(context.regs[REG_RA] == resumeAddress, "resume exception path lost RA");
    require(context.regs[REG_SP] == savedSp, "resume exception path lost SP");
    require(context.regs[REG_FP] == savedFp, "resume exception path lost FP");
    require(context.regs[REG_GP] == savedGp, "resume exception path lost GP");
    for (size_t index = 0; index < savedS.size(); ++index)
    {
        require(context.regs[REG_S0 + index] == savedS[index],
                "resume exception path lost saved S register");
    }
    require(context.hi == 0x0BADF00Du, "resume exception path lost HI mutation");
    require(context.lo == 0x00C0FFEEu, "resume exception path lost LO mutation");

    std::cerr << "[PASS] pending callback regs survive exception resume path\n";
}

void testB017AbortsFurtherCallbackHandling()
{
    using psxrecomp::u32;
    using psxrecomp::runtime::EventMode;
    using psxrecomp::runtime::PsxSystem;
    namespace EventClass = psxrecomp::runtime::EventClass;
    namespace EventSpec = psxrecomp::runtime::EventSpec;

    PsxSystem system;
    assert(system.initialize());

    constexpr u32 callback1 = 0x80012A00u;
    constexpr u32 callback2 = 0x80012A10u;
    const u32 handle1 = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                  EventMode::Callback, callback1);
    const u32 handle2 = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                  EventMode::Callback, callback2);
    assert(handle1 != 0xFFFFFFFFu);
    assert(handle2 != 0xFFFFFFFFu);
    system.events().enableEvent(handle1);
    system.events().enableEvent(handle2);

    std::vector<u32> invoked;
    bool throwOnFirstCallback = true;
    system.setCallbackInvoker(
        [&system, &invoked, &throwOnFirstCallback, callback1, callback2](u32 address) -> u32
        {
            invoked.push_back(address);
            if (address == callback1 && throwOnFirstCallback)
            {
                std::array<u32, 32> regs{};
                regs[9] = 0x17; // B0:ReturnFromException
                system.callBiosVector(0xB0, regs.data(), regs.size());
            }
            if (address == callback2)
            {
                return 2u;
            }
            return 1u;
        });

    triggerVblank(system);
    require(invoked.size() == 1u && invoked[0] == callback1,
            "B0:17 did not abort further callback handling");

    throwOnFirstCallback = false;
    triggerVblank(system);
    require(invoked.size() == 3u && invoked[1] == callback1 && invoked[2] == callback2,
            "callback delivery did not recover after B0:17 abort");

    std::cerr << "[PASS] B0:17 aborts further callback handling correctly\n";
}

void testKernelEventsBeforeHookEntryInt()
{
    using psxrecomp::u32;
    using psxrecomp::runtime::EventMode;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::PsxSystem;
    namespace EventClass = psxrecomp::runtime::EventClass;
    namespace EventSpec = psxrecomp::runtime::EventSpec;

    PsxSystem system;
    assert(system.initialize());

    constexpr u32 descriptorAddress = 0x80014400u;
    constexpr u32 resumeAddress = 0x80012800u;
    constexpr u32 eventCallback = 0x80012840u;
    constexpr u32 savedSp = 0x8001F400u;
    constexpr u32 savedFp = 0x8001F420u;
    constexpr u32 savedGp = 0x8001F440u;
    constexpr std::array<u32, 8> savedS = {
        0xD0000000u, 0xD0000001u, 0xD0000002u, 0xD0000003u,
        0xD0000004u, 0xD0000005u, 0xD0000006u, 0xD0000007u,
    };

    installHookEntryIntDescriptor(system, descriptorAddress, resumeAddress, savedSp, savedFp,
                                  savedGp, savedS);

    const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                 EventMode::Callback, eventCallback);
    assert(handle != 0xFFFFFFFFu);
    system.events().enableEvent(handle);

    CallbackContext context = makeSeededContext();
    std::vector<u32> invoked;
    size_t resumeCalls = 0;
    size_t eventCalls = 0;

    installSyntheticCallbackHarness(
        system, context,
        [&system, &invoked, &resumeCalls, &eventCalls, resumeAddress,
         eventCallback](u32 address, CallbackContext& callbackContext, PsxSystem& systemRef) -> u32
        {
            invoked.push_back(address);
            if (address == eventCallback)
            {
                // PSX-accurate: kernel events fire before HookEntryInt, so the
                // callback sees the original caller context — not the resumed
                // HookEntryInt context.
                ++eventCalls;
                callbackContext.regs[REG_S1] = 0x4444DDDDu;
                callbackContext.hi = 0x5555EEEEu;
                callbackContext.lo = 0x6666FFFFu;
                return 2u;
            }

            if (address == resumeAddress)
            {
                ++resumeCalls;
                callbackContext.hi = 0x1111AAAau;
                callbackContext.lo = 0x2222BBBBu;
                callbackContext.regs[REG_S0] = 0x3333CCCCu;

                std::array<u32, 32> biosRegs{};
                biosRegs[9] = 0x17; // B0:ReturnFromException
                systemRef.callBiosVector(0xB0, biosRegs.data(), biosRegs.size());
            }

            return callbackContext.regs[REG_V0];
        });

    triggerVblank(system);

    require(eventCalls == 1u, "kernel event callback did not run exactly once");
    require(resumeCalls == 1u, "HookEntryInt resume callback did not run exactly once");
    // PSX-accurate ordering: kernel events fire before HookEntryInt.
    require(invoked.size() == 2u && invoked[0] == eventCallback && invoked[1] == resumeAddress,
            "kernel-events-before-HookEntryInt dispatch order was incorrect");
    require((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::VBlank)) == 0u,
            "VBlank was not acknowledged after dispatch");

    system.serviceInterrupts();
    require(resumeCalls == 1u, "HookEntryInt resume callback repeated unexpectedly");
    require(eventCalls == 1u, "kernel event callback repeated unexpectedly");

    // After both phases, the caller context reflects HookEntryInt's resumed
    // state plus the resume body's mutations. The event callback's mutations
    // were discarded (RestoreSaved disposition).
    require(context.regs[REG_V0] == 1u, "caller context lost resumed v0 after dispatch");
    require(context.regs[REG_RA] == resumeAddress, "caller context lost resumed RA after dispatch");
    require(context.regs[REG_SP] == savedSp, "caller context lost resumed SP after dispatch");
    require(context.regs[REG_FP] == savedFp, "caller context lost resumed FP after dispatch");
    require(context.regs[REG_GP] == savedGp, "caller context lost resumed GP after dispatch");
    require(context.regs[REG_S0] == 0x3333CCCCu, "caller context lost committed S0 after dispatch");
    require(context.regs[REG_S1] == savedS[1], "event callback mutated caller S1 unexpectedly");
    require(context.hi == 0x1111AAAau, "event callback mutated caller HI unexpectedly");
    require(context.lo == 0x2222BBBBu, "event callback mutated caller LO unexpectedly");

    std::cerr << "[PASS] kernel events before HookEntryInt with correct caller context\n";
}

void testHookEntryIntFastHeapValidationTripsNearMutation()
{
    using psxrecomp::u32;
    using psxrecomp::runtime::PsxSystem;

    require(::setenv("PSXRECOMP_HEAP_VALIDATE", "1", 1) == 0,
            "failed to enable PSXRECOMP_HEAP_VALIDATE");

    PsxSystem system;
    assert(system.initialize());
    installValidAllocatorHeap(system);

    // Configure a test validator so heap validation is profile-driven.
    psxrecomp::runtime::ValidatorConfig testValidator;
    testValidator.name = "test_allocator";
    testValidator.type = psxrecomp::runtime::ValidatorType::SentinelBlockChain;
    testValidator.currentRoot = AllocatorScanPointerGlobal;
    testValidator.backupRoot = AllocatorBackupPointerGlobal;
    testValidator.header.sizeMask = 0xFFFFFFFCu;
    testValidator.header.freeBit = 0x1u;
    testValidator.header.sentinel = HeapSentinel;
    testValidator.maxNodes = 64;
    system.diagValidators().configure({testValidator});

    constexpr u32 descriptorAddress = 0x80014300u;
    constexpr u32 resumeAddress = 0x80012700u;
    constexpr std::array<u32, 8> savedS = {
        0xC0000000u, 0xC0000001u, 0xC0000002u, 0xC0000003u,
        0xC0000004u, 0xC0000005u, 0xC0000006u, 0xC0000007u,
    };
    CallbackContext context = makeSeededContext();
    installHookEntryIntDescriptor(system, descriptorAddress, resumeAddress, 0x8001F300u,
                                  0x8001F320u, 0x8001F340u, savedS);

    installSyntheticCallbackHarness(
        system, context,
        [resumeAddress](u32 address, CallbackContext&, PsxSystem& systemRef) -> u32
        {
            if (address == resumeAddress)
            {
                systemRef.write<psxrecomp::u32>(HeapBlock0, 0u);
            }
            return 0;
        });

    bool threw = false;
    try
    {
        triggerVblank(system);
    }
    catch (const std::runtime_error& error)
    {
        threw = true;
        require(std::string(error.what()).find("Heap validation failed after HookEntryInt") !=
                    std::string::npos,
                "fast HookEntryInt heap validation threw unexpected error text");
    }
    ::unsetenv("PSXRECOMP_HEAP_VALIDATE");
    if (!threw)
    {
        std::cerr << "expected HookEntryInt fast heap validator failure was not raised\n";
        std::abort();
    }

    std::cerr << "[PASS] HookEntryInt fast heap validator trips near mutation\n";
}
} // namespace

int main()
{
    testRegularCallbackReturnRestoresCallerRegs();
    testHookEntryIntResumeCommitsRestoredRegs();
    testConsumePendingCallbackRegistersAppliedExactlyOnce();
    testPendingCallbackRegsSurviveExceptionResumePath();
    testB017AbortsFurtherCallbackHandling();
    testKernelEventsBeforeHookEntryInt();
    testHookEntryIntFastHeapValidationTripsNearMutation();

    std::cerr << "All callback bridge tests passed.\n";
    return 0;
}