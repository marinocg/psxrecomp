/**
 * @file cdrom_int_promotion_test.cpp
 * @brief Regression test for PR-RV18: CDROM INT3→INT1 promotion.
 *
 * Validates that:
 *  1. ReadN INT3 contains a single stat byte (PSX-SPX).
 *  2. The BIOS handler drains+acks all INT types before delivering events.
 *  3. INT1 correctly promotes after INT3 ack to deliver sector data.
 *  4. Multi-sector BIOS async reads complete without stalling.
 *
 * The root cause was a double stat byte in ReadN INT3 that left a residual
 * byte in the ack response buffer, blocking publishNextInterruptEvent().
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
    system.observeProgramCounter(0x80017400u + (funcId << 2));
    system.callBiosVector(0xA0, regs, 32);
}

bool pumpUntilCdromIrq(PsxSystem& system, u32 maxTicks = 50000)
{
    system.observeProgramCounter(0x80017500u);
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

void setupForRead(PsxSystem& system, u32 locAddr, u8 minute, u8 second, u8 frame)
{
    u8* ram = system.getRam();
    ram[locAddr + 0] = minute;
    ram[locAddr + 1] = second;
    ram[locAddr + 2] = frame;
    ram[locAddr + 3] = 0x00;

    u32 regs[32] = {};
    callA0(system, 0x54, regs); // CdInit
    ackCdromIrq(system);
    pumpUntilCdromIrq(system, 500);
    ackCdromIrq(system);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = locAddr;
    callA0(system, 0x78, regs); // CdAsyncSeekL
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system);
    system.serviceInterrupts();
    pumpUntilCdromIrq(system, 200);
    ackCdromIrq(system);
    system.serviceInterrupts();
}

} // namespace

// ---------------------------------------------------------------
// Test: INT3→INT1 promotion (single stat byte)
//
// Issue ReadN via BIOS, verify INT3 is acked (BIOS general ack),
// then verify INT1 correctly promotes and delivers sector data.
// With the old double-stat-byte bug, INT1 would never promote.
// ---------------------------------------------------------------
static void testInt3ToInt1Promotion()
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
    u8* ram = system.getRam();
    std::memset(ram + readDst, 0xFF, sectorBytes);

    u32 regs[32] = {};
    regs[4] = 1; // count = 1 sector
    regs[5] = readDst;
    regs[6] = 0x00;             // mode
    callA0(system, 0x7E, regs); // CdAsyncReadSector
    assert(regs[2] == 1);

    // Drain ReadN INT3.
    ackCdromIrq(system);
    system.serviceInterrupts();
    ackCdromIrq(system);

    // Pump until INT1 fires — this would hang with the double stat byte bug.
    const bool gotInt1 = pumpUntilCdromIrq(system, 5000);
    assert(gotInt1);

    // Verify sector data was written.
    u32 overwritten = 0;
    for (u32 i = 0; i < sectorBytes; ++i)
    {
        if (ram[readDst + i] != 0xFF)
        {
            ++overwritten;
        }
    }
    assert(overwritten > 0);

    std::cerr << "[PASS] INT3->INT1 promotion: sector data delivered after single-byte INT3 ack\n";
}

// ---------------------------------------------------------------
// Test: 3-sector read completes without stall
//
// A 3-sector BIOS async read must complete all INT1 deliveries
// and end with a Pause command (INT3 + INT2). If the BIOS
// handler doesn't ack intermediate IRQs, the pipeline stalls.
// ---------------------------------------------------------------
static void testMultiSectorCompletion()
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
    constexpr u32 sectorCount = 3;
    u8* ram = system.getRam();
    std::memset(ram + readDst, 0xFF, sectorBytes * sectorCount);

    u32 regs[32] = {};
    regs[4] = sectorCount;
    regs[5] = readDst;
    regs[6] = 0x00;
    callA0(system, 0x7E, regs);
    assert(regs[2] == 1);

    ackCdromIrq(system);
    system.serviceInterrupts();
    ackCdromIrq(system);

    // Pump through all 3 INT1 events (one per sector).
    for (u32 sector = 0; sector < sectorCount; ++sector)
    {
        const bool gotIrq = pumpUntilCdromIrq(system, 5000);
        assert(gotIrq);
    }

    // After the 3rd INT1, CDCMD_PAUSE (0x09) must have been issued.
    assert(system.cdrom().debugSnapshot().currentCommand == 0x09u);

    // Verify all 3 sectors contain TestDisc pattern.
    for (u32 s = 0; s < sectorCount; ++s)
    {
        for (u32 i = 0; i < sectorBytes; ++i)
        {
            assert(ram[readDst + s * sectorBytes + i] == static_cast<u8>((s ^ i) & 0xFFu));
        }
    }

    std::cerr << "[PASS] 3-sector async read: all sectors delivered, Pause issued\n";
}

int main()
{
    testInt3ToInt1Promotion();
    testMultiSectorCompletion();

    std::cerr << "[PASS] All CDROM INT promotion regression tests passed.\n";
    return 0;
}
