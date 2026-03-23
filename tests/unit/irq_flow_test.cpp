/**
 * @file irq_flow_test.cpp
 * @brief Tests for PSX-SPX faithful IRQ exception flow (PR-RV16).
 *
 * Covers:
 *  - Multi-priority chain RFE aborts lower-priority chains
 *  - Chain priority ordering (0 before 1)
 *  - Kernel event RFE prevents HookEntryInt from running
 *  - Split delivery: VBlank + non-VBlank correctly delivered
 */
#include "psxrecomp/runtime/cop0.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <iostream>
#include <vector>

using psxrecomp::u32;
using psxrecomp::runtime::Cop0;
using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::PsxSystem;
namespace EventClass = psxrecomp::runtime::EventClass;
namespace EventSpec = psxrecomp::runtime::EventSpec;
using psxrecomp::runtime::EventMode;

namespace
{

void serviceInterruptsFromTest(PsxSystem& system, u32 pc)
{
    system.observeProgramCounter(pc);
    system.serviceInterrupts();
}

// ---------------------------------------------------------------
// Test 1: Multi-priority chain RFE aborts lower-priority chains
//
// Two chains registered: one at prio 0, one at prio 1.
// Prio 0 handler calls ReturnFromException → prio 1 must never run.
// ---------------------------------------------------------------
static void testMultiPriorityChainRfeAbort()
{
    PsxSystem system;
    assert(system.initialize());

    constexpr u32 prio0Func = 0x80016300;
    constexpr u32 prio1Func = 0x80016310;
    constexpr u32 node0 = 0x80017300;
    constexpr u32 node1 = 0x80017320;

    std::vector<u32> order;
    system.setCallbackInvoker(
        [&system, &order, prio0Func](u32 address) -> u32
        {
            order.push_back(address);
            if (address == prio0Func)
            {
                u32 regs[32] = {};
                regs[9] = 0x17; // ReturnFromException
                system.callBiosVector(0xB0, regs, 32);
            }
            return 0;
        });

    // Register prio 0 chain node.
    system.write<u32>(node0 + 0x00, 0);
    system.write<u32>(node0 + 0x04, 0);
    system.write<u32>(node0 + 0x08, prio0Func);
    system.write<u32>(node0 + 0x0C, 0);

    u32 regs[32] = {};
    regs[9] = 0x02; // SysEnqIntRP
    regs[4] = 0;    // priority 0
    regs[5] = node0;
    system.callBiosVector(0xC0, regs, 32);
    assert(regs[2] == 1);

    // Register prio 1 chain node.
    system.write<u32>(node1 + 0x00, 0);
    system.write<u32>(node1 + 0x04, 0);
    system.write<u32>(node1 + 0x08, prio1Func);
    system.write<u32>(node1 + 0x0C, 0);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[9] = 0x02; // SysEnqIntRP
    regs[4] = 1;    // priority 1
    regs[5] = node1;
    system.callBiosVector(0xC0, regs, 32);
    assert(regs[2] == 1);

    system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
    system.interrupts().raise(InterruptLine::VBlank);
    serviceInterruptsFromTest(system, 0x80017D00u);

    // Only prio 0 handler ran; prio 1 was skipped by RFE abort.
    assert(order.size() == 1);
    assert(order[0] == prio0Func);

    std::cerr << "[PASS] Multi-priority chain RFE aborts lower-priority chains\n";
}

// ---------------------------------------------------------------
// Test 2: Chain handlers fire in priority order (0 before 1)
//
// Both chains return normally (no RFE); verify ordering.
// ---------------------------------------------------------------
static void testChainPriorityOrdering()
{
    PsxSystem system;
    assert(system.initialize());

    constexpr u32 prio0Func = 0x80016400;
    constexpr u32 prio1Func = 0x80016410;
    constexpr u32 eventCallback = 0x80016420;
    constexpr u32 node0 = 0x80017400;
    constexpr u32 node1 = 0x80017420;

    std::vector<u32> order;
    system.setCallbackInvoker(
        [&order](u32 address) -> u32
        {
            order.push_back(address);
            return 0;
        });

    system.write<u32>(node0 + 0x00, 0);
    system.write<u32>(node0 + 0x04, 0);
    system.write<u32>(node0 + 0x08, prio0Func);
    system.write<u32>(node0 + 0x0C, 0);

    u32 regs[32] = {};
    regs[9] = 0x02;
    regs[4] = 0;
    regs[5] = node0;
    system.callBiosVector(0xC0, regs, 32);
    assert(regs[2] == 1);

    system.write<u32>(node1 + 0x00, 0);
    system.write<u32>(node1 + 0x04, 0);
    system.write<u32>(node1 + 0x08, prio1Func);
    system.write<u32>(node1 + 0x0C, 0);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[9] = 0x02;
    regs[4] = 1;
    regs[5] = node1;
    system.callBiosVector(0xC0, regs, 32);
    assert(regs[2] == 1);

    const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                 EventMode::Callback, eventCallback);
    system.events().enableEvent(handle);

    system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
    system.interrupts().raise(InterruptLine::VBlank);
    serviceInterruptsFromTest(system, 0x80017D04u);

    // Order: prio0 chain → prio1 chain → kernel event
    assert(order.size() == 3);
    assert(order[0] == prio0Func);
    assert(order[1] == prio1Func);
    assert(order[2] == eventCallback);

    std::cerr << "[PASS] Chain handlers fire in priority order (0 before 1)\n";
}

// ---------------------------------------------------------------
// Test 3: Kernel event RFE prevents HookEntryInt from running
//
// A kernel event callback calls ReturnFromException.
// HookEntryInt must NOT run.
// ---------------------------------------------------------------
static void testKernelEventRfeSkipsHookEntryInt()
{
    PsxSystem system;
    assert(system.initialize());

    constexpr u32 hookDescriptor = 0x80017500;
    constexpr u32 hookCallback = 0x80016500;
    constexpr u32 eventCallback = 0x80016510;

    // Install HookEntryInt via setjmp + HookEntryInt.
    u32 regs[32] = {};
    regs[9] = 0x13; // setjmp
    regs[4] = hookDescriptor;
    regs[31] = hookCallback;
    regs[29] = 0x80017600;
    regs[30] = 0x80017620;
    system.callBiosVector(0xA0, regs, 32);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[9] = 0x19; // HookEntryInt
    regs[4] = hookDescriptor;
    system.callBiosVector(0xB0, regs, 32);

    // Open kernel event that fires RFE.
    const u32 handle = system.events().openEvent(EventClass::VBlank, EventSpec::Counter,
                                                 EventMode::Callback, eventCallback);
    system.events().enableEvent(handle);

    std::vector<u32> order;
    system.setCallbackInvoker(
        [&system, &order, eventCallback](u32 address) -> u32
        {
            order.push_back(address);
            if (address == eventCallback)
            {
                u32 rfeRegs[32] = {};
                rfeRegs[9] = 0x17; // ReturnFromException
                system.callBiosVector(0xB0, rfeRegs, 32);
            }
            return 0;
        });

    system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank));
    system.interrupts().raise(InterruptLine::VBlank);
    serviceInterruptsFromTest(system, 0x80017D08u);

    // Only the event callback ran; HookEntryInt was skipped by RFE.
    assert(order.size() == 1);
    assert(order[0] == eventCallback);

    std::cerr << "[PASS] Kernel event RFE prevents HookEntryInt from running\n";
}

// ---------------------------------------------------------------
// Test 4: Split delivery: VBlank + Timer0 both fire via HookEntryInt
//
// When VBlank and a non-VBlank IRQ are both pending, the split-delivery
// path delivers non-VBlank first, then restores VBlank for the second
// HookEntryInt invocation.
// ---------------------------------------------------------------
static void testSplitDeliveryBothFire()
{
    PsxSystem system;
    assert(system.initialize());

    constexpr u32 hookDescriptor = 0x80017700;
    constexpr u32 hookCallback = 0x80016700;

    // Install HookEntryInt.
    u32 regs[32] = {};
    regs[9] = 0x13; // setjmp
    regs[4] = hookDescriptor;
    regs[31] = hookCallback;
    regs[29] = 0x80017800;
    regs[30] = 0x80017820;
    system.callBiosVector(0xA0, regs, 32);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[9] = 0x19; // HookEntryInt
    regs[4] = hookDescriptor;
    system.callBiosVector(0xB0, regs, 32);

    int hookInvocations = 0;
    system.setCallbackInvoker(
        [&system, &hookInvocations, hookCallback](u32 address) -> u32
        {
            if (address == hookCallback)
            {
                ++hookInvocations;
                // Call ReturnFromException to complete each invocation.
                u32 rfeRegs[32] = {};
                rfeRegs[9] = 0x17;
                system.callBiosVector(0xB0, rfeRegs, 32);
            }
            return 0;
        });

    // Raise both VBlank and Timer0.
    system.interrupts().writeMask(static_cast<u32>(InterruptLine::VBlank) |
                                  static_cast<u32>(InterruptLine::Timer0));
    system.interrupts().raise(InterruptLine::VBlank);
    system.interrupts().raise(InterruptLine::Timer0);
    serviceInterruptsFromTest(system, 0x80017D0Cu);

    // Split delivery: HookEntryInt fires twice (once for Timer0, once for VBlank).
    assert(hookInvocations == 2);

    // Both IRQs should be acknowledged.
    assert((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::VBlank)) == 0u);
    assert((system.interrupts().readStatus() & static_cast<u32>(InterruptLine::Timer0)) == 0u);

    std::cerr << "[PASS] Split delivery: VBlank + Timer0 both fire via HookEntryInt\n";
}

} // namespace

int main()
{
    testMultiPriorityChainRfeAbort();
    testChainPriorityOrdering();
    testKernelEventRfeSkipsHookEntryInt();
    testSplitDeliveryBothFire();

    std::cerr << "\nAll IRQ flow tests passed.\n";
    return 0;
}
