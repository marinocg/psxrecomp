/**
 * @file bios_cd_int1_test.cpp
 * @brief INT1 handler hardening tests (PR-RV11).
 *
 * Covers: 2-sector address advance, FIFO no-spill, and completion path
 * (CDCMD_PAUSE on last sector / preload on non-last).
 */
#include "psxrecomp/runtime/disc.h"
#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>

using psxrecomp::u32;
using psxrecomp::u8;
using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::PsxSystem;

namespace
{

class TestDisc : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
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

void callA0(PsxSystem& system, u32 funcId, u32* regs)
{
    regs[9] = funcId;
    system.callBiosVector(0xA0, regs, 32);
}

bool pumpUntilCdromIrq(PsxSystem& system, u32 maxTicks = 50000)
{
    for (u32 i = 0; i < maxTicks; ++i)
    {
        system.tickCpuCycles(2048);
        if (system.cdrom().hasIrqRequest())
        {
            system.serviceInterrupts();
            return true;
        }
        system.serviceInterrupts();
    }
    return false;
}

void ackCdromIrq(PsxSystem& system)
{
    if ((system.cdrom().readStatus() & (1u << 5)) != 0u)
    {
        (void)system.cdrom().readResponse();
    }
    system.cdrom().writeInterruptFlags(0x07u);
}

/// Full setup: init -> drain init irqs -> seek to given MSF -> drain seek irqs.
void setupForRead(PsxSystem& system, u32 locAddr, u8 minute, u8 second, u8 frame)
{
    u8* ram = system.getRam();
    ram[locAddr + 0] = minute;
    ram[locAddr + 1] = second;
    ram[locAddr + 2] = frame;
    ram[locAddr + 3] = 0x00;

    u32 regs[32] = {};
    callA0(system, 0x54, regs); // CdInit
    ackCdromIrq(system);        // INT3
    pumpUntilCdromIrq(system, 500);
    ackCdromIrq(system); // INT2

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = locAddr;
    callA0(system, 0x78, regs); // CdAsyncSeekL
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system); // SeekL INT3
    system.serviceInterrupts();
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system); // SeekL INT2
    system.serviceInterrupts();
}

} // namespace

// ---------------------------------------------------------------
// Test 11: 2-sector address advance
//
// A 2-sector ReadSector call must place sector 0 at dst and sector 1
// at dst + sectorBytes.  After the first INT1, sector 1's region is
// still untouched; after the second INT1 it holds LBA 1 content.
// ---------------------------------------------------------------
static void testCdAsyncReadSector2SectorAddressAdvance()
{
    PsxSystem system;
    auto disc = std::make_shared<TestDisc>();
    system.setDisc(disc);
    assert(system.initialize());
    system.interrupts().writeMask(system.interrupts().readMask() |
                                  static_cast<u32>(InterruptLine::Cdrom));

    setupForRead(system, 0xA000, 0x00, 0x02, 0x00); // seek to LBA 0

    constexpr u32 readDst = 0xB000;
    constexpr u32 sectorBytes = 0x800;
    u8* ram = system.getRam();
    std::memset(ram + readDst, 0xFF, sectorBytes * 2); // sentinel

    u32 regs[32] = {};
    regs[4] = 2; // count
    regs[5] = readDst;
    regs[6] = 0x00; // mode
    callA0(system, 0x7E, regs);
    assert(regs[2] == 1);

    // Drain ReadN INT3 left visible after A0:7E.
    ackCdromIrq(system);
    system.serviceInterrupts();
    ackCdromIrq(system);

    // First INT1: sector 0 copied.
    assert(pumpUntilCdromIrq(system, 5000));
    // Sector 0 region must be overwritten (not all 0xFF).
    {
        u32 unchanged = 0;
        for (u32 i = 0; i < sectorBytes; ++i)
        {
            unchanged += (ram[readDst + i] == 0xFF) ? 1u : 0u;
        }
        assert(unchanged < sectorBytes);
    }
    // Sector 1 region must still be all 0xFF (no spill from sector 0 INT1).
    for (u32 i = 0; i < sectorBytes; ++i)
    {
        assert(ram[readDst + sectorBytes + i] == 0xFF);
    }

    // Second INT1: sector 1 copied.
    assert(pumpUntilCdromIrq(system, 5000));
    // Verify sector 1 contains TestDisc pattern for LBA 1.
    for (u32 i = 0; i < sectorBytes; ++i)
    {
        assert(ram[readDst + sectorBytes + i] == static_cast<u8>((1u ^ i) & 0xFF));
    }

    std::cerr << "[PASS] 2-sector address advance: each INT1 writes to dst + N*sectorBytes\n";
}

// ---------------------------------------------------------------
// Test 12: FIFO no-spill — single INT1 writes exactly sectorBytes
//
// After one INT1, bytes beyond position sectorBytes in the destination
// buffer must remain unchanged (the handler did not over-drain).
// ---------------------------------------------------------------
static void testCdAsyncReadSectorFifoNoSpill()
{
    PsxSystem system;
    auto disc = std::make_shared<TestDisc>();
    system.setDisc(disc);
    assert(system.initialize());
    system.interrupts().writeMask(system.interrupts().readMask() |
                                  static_cast<u32>(InterruptLine::Cdrom));

    setupForRead(system, 0xA000, 0x00, 0x02, 0x00);

    constexpr u32 readDst = 0xB000;
    constexpr u32 sectorBytes = 0x800;
    constexpr u32 guardBytes = 0x800; // region that must stay as sentinel
    u8* ram = system.getRam();
    std::memset(ram + readDst, 0x42, sectorBytes + guardBytes);

    u32 regs[32] = {};
    regs[4] = 1;
    regs[5] = readDst;
    regs[6] = 0x00;
    callA0(system, 0x7E, regs);
    assert(regs[2] == 1);

    ackCdromIrq(system);
    system.serviceInterrupts();
    ackCdromIrq(system);

    assert(pumpUntilCdromIrq(system, 5000));

    // Entire [readDst + sectorBytes, readDst + sectorBytes + guardBytes) must remain 0x42.
    for (u32 i = 0; i < guardBytes; ++i)
    {
        assert(ram[readDst + sectorBytes + i] == 0x42u);
    }

    std::cerr << "[PASS] FIFO no-spill: INT1 writes exactly sectorBytes, no overflow\n";
}

// ---------------------------------------------------------------
// Test 13: Completion path
//
// For a 2-sector read:
//   - After the 1st INT1 (non-last sector), the CDROM command must NOT
//     be CDCMD_PAUSE (the controller keeps reading).
//   - After the 2nd INT1 (last sector), the CDROM command must be
//     CDCMD_PAUSE (0x09).
// ---------------------------------------------------------------
static void testCdAsyncReadSectorCompletionPath()
{
    PsxSystem system;
    auto disc = std::make_shared<TestDisc>();
    system.setDisc(disc);
    assert(system.initialize());
    system.interrupts().writeMask(system.interrupts().readMask() |
                                  static_cast<u32>(InterruptLine::Cdrom));

    setupForRead(system, 0xA000, 0x00, 0x02, 0x00);

    u8* ram = system.getRam();
    constexpr u32 readDst = 0xB000;
    std::memset(ram + readDst, 0xAA, 0x800 * 2);

    u32 regs[32] = {};
    regs[4] = 2;
    regs[5] = readDst;
    regs[6] = 0x00;
    callA0(system, 0x7E, regs);
    assert(regs[2] == 1);

    ackCdromIrq(system);
    system.serviceInterrupts();
    ackCdromIrq(system);

    // After 1st INT1 (non-last sector), command must not be CDCMD_PAUSE (0x09).
    assert(pumpUntilCdromIrq(system, 5000));
    assert(system.cdrom().debugSnapshot().currentCommand != 0x09u);

    // After 2nd INT1 (last sector), command must be CDCMD_PAUSE (0x09).
    assert(pumpUntilCdromIrq(system, 5000));
    assert(system.cdrom().debugSnapshot().currentCommand == 0x09u);

    std::cerr << "[PASS] Completion path: last sector issues CDCMD_PAUSE, non-last does not\n";
}

// ---------------------------------------------------------------
// Test 14: Full end-to-end async read flow
//
// Mirrors the sequence a game would use through BIOS:
//   CdInit -> CdAsyncSeekL -> CdAsyncReadSector (3 sectors) ->
//   poll INT1 x3 -> verify data -> verify completion (CDCMD_PAUSE)
// ---------------------------------------------------------------
static void testCdAsyncFullReadFlow()
{
    PsxSystem system;
    auto disc = std::make_shared<TestDisc>();
    system.setDisc(disc);
    assert(system.initialize());
    system.interrupts().writeMask(system.interrupts().readMask() |
                                  static_cast<u32>(InterruptLine::Cdrom));

    // CdInit + drain init IRQs.
    u32 regs[32] = {};
    callA0(system, 0x54, regs);
    assert(regs[2] == 1);
    ackCdromIrq(system);
    pumpUntilCdromIrq(system, 500);
    ackCdromIrq(system);

    // Seek to LBA 5 (MSF 00:02:05 BCD).
    u8* ram = system.getRam();
    constexpr u32 locAddr = 0xA000;
    ram[locAddr + 0] = 0x00;
    ram[locAddr + 1] = 0x02;
    ram[locAddr + 2] = 0x05;
    ram[locAddr + 3] = 0x00;

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = locAddr;
    callA0(system, 0x78, regs); // CdAsyncSeekL
    assert(regs[2] == 1);
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system);
    system.serviceInterrupts();
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system);
    system.serviceInterrupts();

    // Read 3 sectors at destination 0xB000, mode 0x00 (ReadN, 0x800 bytes).
    constexpr u32 readDst = 0xB000;
    constexpr u32 sectorBytes = 0x800;
    std::memset(ram + readDst, 0xFF, sectorBytes * 3);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = 3;
    regs[5] = readDst;
    regs[6] = 0x00;
    callA0(system, 0x7E, regs);
    assert(regs[2] == 1);

    // Drain ReadN INT3.
    ackCdromIrq(system);
    system.serviceInterrupts();
    ackCdromIrq(system);

    // Pump three INT1 events.
    for (u32 sector = 0; sector < 3; ++sector)
    {
        assert(pumpUntilCdromIrq(system, 5000));
        // Verify this sector contains the TestDisc pattern for LBA (5+sector).
        for (u32 i = 0; i < sectorBytes; ++i)
        {
            assert(ram[readDst + sector * sectorBytes + i] ==
                   static_cast<u8>(((5u + sector) ^ i) & 0xFF));
        }
    }

    // After the 3rd INT1, CDCMD_PAUSE (0x09) must have been issued.
    assert(system.cdrom().debugSnapshot().currentCommand == 0x09u);

    std::cerr
        << "[PASS] Full async read flow: CdInit -> SeekL -> ReadSector(3) -> INT1x3 -> Pause\n";
}

int main()
{
    testCdAsyncReadSector2SectorAddressAdvance();
    testCdAsyncReadSectorFifoNoSpill();
    testCdAsyncReadSectorCompletionPath();
    testCdAsyncFullReadFlow();

    std::cerr << "\nAll INT1 hardening tests passed.\n";
    return 0;
}
