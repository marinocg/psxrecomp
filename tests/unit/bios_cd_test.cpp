/**
 * @file bios_cd_test.cpp
 * @brief Tests for the A0-vector CD-ROM BIOS functions (CdInit, CdRemove,
 *        CdAsyncSeekL, CdAsyncGetStatus, CdAsyncReadSector,
 *        CdAsyncSetMode, CdInitSubFunc).
 *
 * Each test exercises the BIOS call through PsxSystem::callBiosVector and
 * verifies completion via the kernel event system.
 */
#include "psxrecomp/runtime/disc.h"
#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

using psxrecomp::u32;
using psxrecomp::u8;
namespace EventClass = psxrecomp::runtime::EventClass;
namespace EventSpec = psxrecomp::runtime::EventSpec;
using psxrecomp::runtime::EventMode;
using psxrecomp::runtime::EventStatus;
using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::LogLevel;
using psxrecomp::runtime::PsxSystem;

namespace
{

/// Minimal disc backend that serves deterministic sector content.
class TestDisc : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        // Fill with a pattern: byte = (lba ^ index) & 0xFF
        for (u32 i = 0; i < 2048; ++i)
        {
            out[i] = static_cast<u8>((lba ^ i) & 0xFFu);
        }
        return true;
    }

    u32 userSectorCount() const override
    {
        return 1000;
    }
};

/// Helper: call a BIOS A0 vector function.
void callA0(PsxSystem& system, u32 funcId, u32* regs)
{
    regs[9] = funcId;
    system.callBiosVector(0xA0, regs, 32);
}

/// Helper: tick hardware + service interrupts until a CDROM IRQ fires
/// or a maximum iteration count is reached.
bool pumpUntilCdromIrq(PsxSystem& system, u32 maxTicks = 50000)
{
    for (u32 i = 0; i < maxTicks; ++i)
    {
        system.tickCpuCycles(2048);
        system.serviceInterrupts();
        if (system.cdrom().hasIrqRequest())
        {
            system.serviceInterrupts();
            return true;
        }
    }
    return false;
}

/// Helper: acknowledge the current CDROM interrupt.
void ackCdromIrq(PsxSystem& system)
{
    system.cdrom().writeInterruptFlags(0x07u);
}

/// Helper: initialise system with a disc attached.
void initWithDisc(PsxSystem& system, std::shared_ptr<psxrecomp::runtime::Disc> disc)
{
    system.setDisc(disc);
    assert(system.initialize());
    // Enable CDROM interrupt line in the interrupt controller.
    system.interrupts().writeMask(system.interrupts().readMask() |
                                  static_cast<u32>(InterruptLine::Cdrom));
}

} // namespace

// ---------------------------------------------------------------
// Test 1: CdInit (A0:54) returns 1 and initialises BIOS CD state
// ---------------------------------------------------------------
static void testCdInit()
{
    PsxSystem system;
    auto disc = std::make_shared<TestDisc>();
    initWithDisc(system, disc);

    u32 regs[32] = {};
    callA0(system, 0x54, regs);
    assert(regs[2] == 1);

    // After CdInit, the Init command (0x0A) was issued and should
    // produce an INT3 (ack).  Pump until the IRQ is visible.
    assert(system.cdrom().hasIrqRequest());
    assert((system.cdrom().readInterruptFlags() & 0x07u) != 0u);

    std::cerr << "[PASS] CdInit returns 1 and issues Init command\n";
}

// ---------------------------------------------------------------
// Test 2: CdRemove (A0:56) returns 1
// ---------------------------------------------------------------
static void testCdRemove()
{
    PsxSystem system;
    assert(system.initialize());

    u32 regs[32] = {};
    callA0(system, 0x54, regs); // init first
    std::fill(std::begin(regs), std::end(regs), 0u);
    callA0(system, 0x56, regs);
    assert(regs[2] == 1);

    std::cerr << "[PASS] CdRemove returns 1\n";
}

// ---------------------------------------------------------------
// Test 3: CdAsyncSetMode (A0:81) issues Setmode and delivers event
// ---------------------------------------------------------------
static void testCdAsyncSetMode()
{
    PsxSystem system;
    auto disc = std::make_shared<TestDisc>();
    initWithDisc(system, disc);

    // Initialise the CD subsystem.
    u32 regs[32] = {};
    callA0(system, 0x54, regs);
    ackCdromIrq(system);

    // Open a user event for CommandDone to observe completion.
    const u32 evHandle = system.events().openEvent(EventClass::Cdrom, EventSpec::CommandDone,
                                                   EventMode::NoCallback, 0);
    assert(evHandle != 0xFFFFFFFFu);
    system.events().enableEvent(evHandle);

    // CdAsyncSetMode with mode = 0x80 (double speed).
    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = 0x80; // mode byte
    callA0(system, 0x81, regs);
    assert(regs[2] == 1);

    // Pump until Setmode INT3 fires; serviceBiosCdromInterrupt delivers CommandDone.
    assert(pumpUntilCdromIrq(system, 200));
    system.serviceInterrupts();
    assert(system.events().isEventDelivered(evHandle));

    std::cerr << "[PASS] CdAsyncSetMode delivers CommandDone event\n";
}

// ---------------------------------------------------------------
// Test 4: CdAsyncGetStatus (A0:7C) copies stat byte to result buffer
// ---------------------------------------------------------------
static void testCdAsyncGetStatus()
{
    PsxSystem system;
    auto disc = std::make_shared<TestDisc>();
    initWithDisc(system, disc);

    u32 regs[32] = {};
    callA0(system, 0x54, regs);
    ackCdromIrq(system);

    // Prepare result buffer in RAM at offset 0x8000.
    constexpr u32 resultAddr = 0x8000;
    system.getRam()[resultAddr] = 0xFF; // sentinel

    // Open event for CommandDone.
    const u32 evHandle = system.events().openEvent(EventClass::Cdrom, EventSpec::CommandDone,
                                                   EventMode::NoCallback, 0);
    assert(evHandle != 0xFFFFFFFFu);
    system.events().enableEvent(evHandle);

    // Issue CdAsyncGetStatus.
    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = resultAddr;
    callA0(system, 0x7C, regs);
    assert(regs[2] == 1);

    // Pump until Getstat INT3 fires; serviceBiosCdromInterrupt copies stat + delivers CommandDone.
    assert(pumpUntilCdromIrq(system, 200));
    system.serviceInterrupts();
    assert(system.events().isEventDelivered(evHandle));

    // The result buffer should contain the stat byte (motor-on = 0x02).
    assert(system.getRam()[resultAddr] != 0xFF);       // must have been overwritten
    assert((system.getRam()[resultAddr] & 0x02) != 0); // motor-on bit set after CdInit

    std::cerr << "[PASS] CdAsyncGetStatus copies stat byte to buffer\n";
}

// ---------------------------------------------------------------
// Test 5: CdAsyncSeekL (A0:78) issues Setloc + SeekL
// ---------------------------------------------------------------
static void testCdAsyncSeekL()
{
    PsxSystem system;
    auto disc = std::make_shared<TestDisc>();
    initWithDisc(system, disc);

    u32 regs[32] = {};
    callA0(system, 0x54, regs);
    ackCdromIrq(system);

    // Open event for CommandDone (INT2 = SeekL completion).
    const u32 evHandle = system.events().openEvent(EventClass::Cdrom, EventSpec::CommandDone,
                                                   EventMode::NoCallback, 0);
    assert(evHandle != 0xFFFFFFFFu);
    system.events().enableEvent(evHandle);

    // Write CdlLOC to RAM: minute=0x00, second=0x02, sector=0x00, pad=0x00
    constexpr u32 locAddr = 0x9000;
    u8* ram = system.getRam();
    ram[locAddr + 0] = 0x00; // minute (BCD)
    ram[locAddr + 1] = 0x02; // second (BCD)
    ram[locAddr + 2] = 0x00; // sector (BCD)
    ram[locAddr + 3] = 0x00; // pad

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = locAddr;
    callA0(system, 0x78, regs);
    assert(regs[2] == 1);

    // SeekL: the CDROM fires INT3 (first response) immediately, then INT2 (seek complete)
    // only after the mechanical seek finishes.  Drain any immediate INT3s, then pump
    // for INT2; serviceBiosCdromInterrupt delivers CommandDone on INT2.
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system);
    system.serviceInterrupts();
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system);
    system.serviceInterrupts();
    assert(pumpUntilCdromIrq(system, 500));
    system.serviceInterrupts();
    assert(system.events().isEventDelivered(evHandle));

    std::cerr << "[PASS] CdAsyncSeekL delivers CommandDone on completion\n";
}

// ---------------------------------------------------------------
// Test 6: CdAsyncReadSector (A0:7E) reads sector data into RAM
// ---------------------------------------------------------------
static void testCdAsyncReadSector()
{
    PsxSystem system;
    auto disc = std::make_shared<TestDisc>();
    initWithDisc(system, disc);

    u32 regs[32] = {};
    callA0(system, 0x54, regs);
    ackCdromIrq(system);

    // Ack the Init INT2 as well.
    pumpUntilCdromIrq(system, 500);
    ackCdromIrq(system);

    // Set location to LBA 0 (00:02:00 in BCD MSF).
    constexpr u32 locAddr = 0xA000;
    u8* ram = system.getRam();
    ram[locAddr + 0] = 0x00;
    ram[locAddr + 1] = 0x02;
    ram[locAddr + 2] = 0x00;
    ram[locAddr + 3] = 0x00;
    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = locAddr;
    callA0(system, 0x78, regs); // SeekL to 00:02:00

    // Drain intermediate INT3s and wait for INT2 (seek complete).
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system);
    system.serviceInterrupts();
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system);
    system.serviceInterrupts();
    pumpUntilCdromIrq(system, 500); // SeekL INT2
    ackCdromIrq(system);
    system.serviceInterrupts();

    // Open event for data-ready (INT1 → spec 0x0010 = CommandAck).
    const u32 dataEvHandle = system.events().openEvent(EventClass::Cdrom, EventSpec::CommandAck,
                                                       EventMode::NoCallback, 0);
    assert(dataEvHandle != 0xFFFFFFFFu);
    system.events().enableEvent(dataEvHandle);

    // CdAsyncReadSector: 1 sector, destination = 0xB000, mode = 0x00.
    constexpr u32 readDst = 0xB000;
    std::memset(ram + readDst, 0xFF, 2048); // sentinel fill

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = 1;       // count
    regs[5] = readDst; // destination
    regs[6] = 0x00;    // mode
    callA0(system, 0x7E, regs);
    assert(regs[2] == 1);

    // Setmode INT3 + ReadN INT3 should fire immediately.
    ackCdromIrq(system);
    system.serviceInterrupts();
    ackCdromIrq(system);

    // Pump until INT1 (data ready) fires — needs tick for sector timing.
    [[maybe_unused]] bool gotData = pumpUntilCdromIrq(system, 5000);
    assert(gotData);

    // The event should be delivered.
    assert(system.events().isEventDelivered(dataEvHandle));

    // The destination buffer should contain the TestDisc pattern for LBA 0.
    [[maybe_unused]] bool dataMatches = true;
    for (u32 i = 0; i < 2048; ++i)
    {
        const u8 expected = static_cast<u8>((0 ^ i) & 0xFF);
        if (ram[readDst + i] != expected)
        {
            dataMatches = false;
            break;
        }
    }
    assert(dataMatches);

    std::cerr << "[PASS] CdAsyncReadSector copies sector data to RAM\n";
}

// ---------------------------------------------------------------
// Test 7: CdInitSubFunc (A0:95) returns 1 and ensures init
// ---------------------------------------------------------------
static void testCdInitSubFunc()
{
    PsxSystem system;
    assert(system.initialize());

    u32 regs[32] = {};
    callA0(system, 0x95, regs);
    assert(regs[2] == 1);

    std::cerr << "[PASS] CdInitSubFunc returns 1\n";
}

// ---------------------------------------------------------------
// Test 8: SDK-style async CD flow (init → seek → read → event)
//
// Simulates the sequence a homebrew game would use through BIOS:
//   CdInit → CdAsyncSeekL → CdAsyncReadSector → poll events
// ---------------------------------------------------------------
static void testSdkStyleAsyncCdFlow()
{
    PsxSystem system;
    auto disc = std::make_shared<TestDisc>();
    initWithDisc(system, disc);

    u32 regs[32] = {};

    // 1. CdInit
    callA0(system, 0x54, regs);
    ackCdromIrq(system);
    pumpUntilCdromIrq(system, 500);
    ackCdromIrq(system);

    // 2. Open events for completion + data-ready
    const u32 doneEv = system.events().openEvent(EventClass::Cdrom, EventSpec::CommandDone,
                                                 EventMode::NoCallback, 0);
    const u32 dataEv = system.events().openEvent(EventClass::Cdrom, EventSpec::CommandAck,
                                                 EventMode::NoCallback, 0);
    assert(doneEv != 0xFFFFFFFFu);
    assert(dataEv != 0xFFFFFFFFu);
    system.events().enableEvent(doneEv);
    system.events().enableEvent(dataEv);

    // 3. Seek to sector 10 (00:02:10 in BCD MSF).
    u8* ram = system.getRam();
    constexpr u32 locAddr = 0xC000;
    ram[locAddr + 0] = 0x00; // minute
    ram[locAddr + 1] = 0x02; // second
    ram[locAddr + 2] = 0x10; // frame (BCD 10)
    ram[locAddr + 3] = 0x00;

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = locAddr;
    callA0(system, 0x78, regs);
    assert(regs[2] == 1);

    // Drain intermediate INT3s and pump for INT2 (seek complete = CommandDone).
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system);
    system.serviceInterrupts();
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system);
    system.serviceInterrupts();
    assert(pumpUntilCdromIrq(system, 500));
    system.serviceInterrupts();

    // The CommandDone event should be delivered from SeekL INT2.
    assert(system.events().isEventDelivered(doneEv));
    // Consume the delivery (reset to Enabled for the read phase).
    system.events().testEvent(doneEv);

    // 4. Read 1 sector into RAM at 0xD000.
    constexpr u32 readDst = 0xD000;
    std::memset(ram + readDst, 0xAA, 2048);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = 1;
    regs[5] = readDst;
    regs[6] = 0x00;
    callA0(system, 0x7E, regs);
    assert(regs[2] == 1);

    ackCdromIrq(system);
    system.serviceInterrupts();
    ackCdromIrq(system);

    // Pump for data-ready.
    [[maybe_unused]] bool gotData = pumpUntilCdromIrq(system, 5000);
    assert(gotData);
    assert(system.events().isEventDelivered(dataEv));

    // Verify sector content matches TestDisc for LBA 10.
    [[maybe_unused]] bool dataOk = true;
    for (u32 i = 0; i < 2048; ++i)
    {
        const u8 expected = static_cast<u8>((10 ^ i) & 0xFF);
        if (ram[readDst + i] != expected)
        {
            dataOk = false;
            break;
        }
    }
    assert(dataOk);

    std::cerr << "[PASS] SDK-style async CD flow (init → seek → read → event)\n";
}

int main()
{
    testCdInit();
    testCdRemove();
    testCdAsyncSetMode();
    testCdAsyncGetStatus();
    testCdAsyncSeekL();
    testCdAsyncReadSector();
    testCdInitSubFunc();
    testSdkStyleAsyncCdFlow();

    std::cerr << "\nAll BIOS CD tests passed.\n";
    return 0;
}
