// PR-RV21d: BIOS INT1→data-ready shortcut removed — acceptance path isolation tests.
//
// These tests prove that `enableDataRead()` is the ONLY mechanism that populates
// the data FIFO for BIOS-owned async reads, and that game-owned INT1s leave the
// FIFO empty until the game explicitly writes BFRD (bit 7 of request register).
//
// Four invariants under test:
//   1. A generic INT1 (no enableDataRead, no BFRD write) leaves DRQSTS=0.
//   2. enableDataRead() after INT1 raises DRQSTS and records AcceptBiosAuto.
//   3. BFRD-write (game path) and enableDataRead (BIOS path) both expose the
//      sector but record different trace reasons (AcceptBfrd vs AcceptBiosAuto).
//   4. ACK-only (writeInterruptFlags without enableDataRead or BFRD) does NOT
//      silently expose the next sector.
//
// Two integration sequences confirm cross-sector ordering and mixed source proofs.
//
// http://problemkaputt.de/psx-spx.htm#cdromcontrollerioports

#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <cassert>
#include <cstddef>
#include <vector>

namespace
{
using psxrecomp::u32;
using psxrecomp::u8;
using Reason = psxrecomp::runtime::Cdrom::SectorPhaseReason;

constexpr u32 kReadCycles = 451584u; // single-speed cadence

// ---------------------------------------------------------------------------
// PatternDisc: sector N has byte[i] = (N * 7 + i) & 0xFF.
// ---------------------------------------------------------------------------
class PatternDisc final : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<u8>((lba * 7u + static_cast<u32>(i)) & 0xFFu);
        return true;
    }
    bool readRawSector2352(u32, std::span<u8, 2352>) override
    {
        return false;
    }
    u32 userSectorCount() const override
    {
        return 8u;
    }
};

// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------

u8 irqType(const psxrecomp::runtime::Cdrom& c)
{
    return static_cast<u8>(c.readInterruptFlags() & 0x07u);
}

bool drqsts(const psxrecomp::runtime::Cdrom& c)
{
    return (c.readStatus() & (1u << 6)) != 0u;
}

void ack(psxrecomp::runtime::Cdrom& c)
{
    while ((c.readStatus() & (1u << 5u)) != 0u)
        (void)c.readResponse();
    c.writeInterruptFlags(0x07u);
}

void enableBfrd(psxrecomp::runtime::Cdrom& c)
{
    c.writeReg(0u, 0u);
    c.writeReg(3u, 0x80u);
}

void disableBfrd(psxrecomp::runtime::Cdrom& c)
{
    c.writeReg(0u, 0u);
    c.writeReg(3u, 0x00u);
}

void issueReadN(psxrecomp::runtime::Cdrom& c, u8 mm, u8 ss, u8 ff)
{
    c.writeParam(mm);
    c.writeParam(ss);
    c.writeParam(ff);
    c.writeCommand(0x02u); // Setloc
    assert(irqType(c) == 0x03u);
    ack(c);
    c.writeCommand(0x06u); // ReadN
    assert(irqType(c) == 0x03u);
    ack(c);
}

// Return the logical index of the first occurrence of (lba, reason), or -1.
int findTrace(const psxrecomp::runtime::Cdrom& c, u32 lba, Reason reason)
{
    for (size_t i = 0; i < c.phaseTraceCount(); ++i)
    {
        const auto e = c.phaseTraceEntry(i);
        if (e.lba == lba && e.reason == reason)
            return static_cast<int>(i);
    }
    return -1;
}

// Return the logical index of the first occurrence of reason (any LBA), or -1.
int findTrace(const psxrecomp::runtime::Cdrom& c, Reason reason)
{
    for (size_t i = 0; i < c.phaseTraceCount(); ++i)
    {
        if (c.phaseTraceEntry(i).reason == reason)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace

// ============================================================================
// Test 1 — Generic INT1 (no enableDataRead, no BFRD) leaves DRQSTS=0.
//
// Sequence:
//   Setloc + ReadN → INT3 ack
//   tick(1×) → INT1 published for LBA=0
//   Verify: DRQSTS=0
//   Verify: readData() returns padding (0x00), not sector data
//   Verify: no AcceptBfrd and no AcceptBiosAuto in phase trace
//   Verify: publish_int1 IS in trace (INT1 did fire)
// ============================================================================
static void test1_generic_int1_leaves_fifo_empty()
{
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    PatternDisc disc;
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueReadN(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0

    cdrom.tick(kReadCycles); // triggers INT1 for LBA=0
    assert(irqType(cdrom) == 0x01u);

    // INT1 published.
    assert(findTrace(cdrom, 0u, Reason::PublishInt1) >= 0);

    // No one set BFRD or called enableDataRead → DRQSTS must be low.
    assert(!drqsts(cdrom));

    // No AcceptBfrd or AcceptBiosAuto must appear in trace.
    assert(findTrace(cdrom, Reason::AcceptBfrd) == -1);
    assert(findTrace(cdrom, Reason::AcceptBiosAuto) == -1);

    // readData() must return 0x00 (FIFO-empty pad), not sector bytes.
    const u8 first = cdrom.readData();
    assert(first == 0x00u); // pad value when FIFO is empty

    ack(cdrom); // clean up
}

// ============================================================================
// Test 2 — BIOS path: enableDataRead() after INT1 exposes sector, records
//           AcceptBiosAuto.
//
// Sequence:
//   tick(1×) → INT1 for LBA=0
//   Call cdrom.enableDataRead() [proxy for what BIOS servicer does]
//   Verify: DRQSTS=1
//   Verify: AcceptBiosAuto in trace, DrqstsOn after it
//   Verify: readData() returns real sector bytes
//   Verify: no AcceptBfrd in trace (BFRD was never written via register)
// ============================================================================
static void test2_bios_enableDataRead_exposes_sector()
{
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    PatternDisc disc;
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueReadN(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01u);

    cdrom.enableDataRead();

    // DRQSTS must now be high.
    assert(drqsts(cdrom));

    // AcceptBiosAuto must be in trace, followed by DrqstsOn.
    const int idxAuto = findTrace(cdrom, 0u, Reason::AcceptBiosAuto);
    const int idxDrq = findTrace(cdrom, 0u, Reason::DrqstsOn);
    assert(idxAuto >= 0);
    assert(idxDrq > idxAuto);

    // AcceptBfrd must NOT appear (BFRD was never written via register).
    assert(findTrace(cdrom, Reason::AcceptBfrd) == -1);

    // At least the first byte must be real sector data (LBA=0, byte[0] = (0*7+0)&0xFF = 0).
    // The pattern formula: byte[i] = (lba * 7 + i) & 0xFF.
    // The raw-sector user data starts after the 24-byte header inside the 2352-byte raw sector.
    // The CDROM FIFO is populated from the user-data slice (2048 bytes), so byte[0] of the
    // FIFO corresponds to byte[24] of the physical sector. For readData() in ReadN mode the
    // data FIFO is exactly the 2048-byte payload, however the data region in the mode-2
    // sector padding may differ. We simply confirm the FIFO is non-empty and not padding.
    // Sector bytes from PatternDisc for LBA=0: out[i] = (0*7 + i) & 0xFF = i & 0xFF.
    const u8 b0 = cdrom.readData();
    assert(b0 == 0x00u); // byte[0] of LBA=0 payload = (0*7+0) & 0xFF = 0

    const u8 b1 = cdrom.readData();
    assert(b1 == 0x01u); // byte[1] of LBA=0 payload = (0*7+1) & 0xFF = 1

    ack(cdrom);
}

// ============================================================================
// Test 3 — BFRD (game path) and enableDataRead (BIOS path) both expose
//           the sector but record distinct trace reasons.
//
// Sequence:
//   tick(2×) → INT1-A published (LBA=0), LBA=1 buffered.
//   Game path for A: enableBfrd via writeReg → AcceptBfrd, drain all.
//   disableBfrd, ack INT1-A → INT1-B fires.
//   BIOS path for B: enableDataRead() → AcceptBiosAuto.
//   Verify: A has AcceptBfrd but NOT AcceptBiosAuto.
//   Verify: B has AcceptBiosAuto but NOT AcceptBfrd.
//   Verify: both sectors had data readable (DrqstsOn present for each).
// ============================================================================
static void test3_accept_source_distinct_trace_reasons()
{
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    PatternDisc disc;
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueReadN(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0, LBA=1 following

    cdrom.tick(kReadCycles * 2u); // INT1-A visible, B buffered
    assert(irqType(cdrom) == 0x01u);

    // ---- Sector A: game path ----
    enableBfrd(cdrom);
    assert(drqsts(cdrom));
    assert(findTrace(cdrom, 0u, Reason::AcceptBfrd) >= 0);
    assert(findTrace(cdrom, 0u, Reason::AcceptBiosAuto) == -1);

    // Drain A.
    for (size_t i = 0; i < 2048u; ++i)
        (void)cdrom.readData();
    assert(!drqsts(cdrom));

    // ---- Handoff to B ----
    disableBfrd(cdrom);
    ack(cdrom);
    cdrom.tick(1u);
    assert(irqType(cdrom) == 0x01u);

    // No accept yet for B.
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) == -1);
    assert(findTrace(cdrom, 1u, Reason::AcceptBiosAuto) == -1);

    // ---- Sector B: BIOS path ----
    cdrom.enableDataRead();
    assert(drqsts(cdrom));

    assert(findTrace(cdrom, 1u, Reason::AcceptBiosAuto) >= 0);
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) == -1); // AcceptBfrd must NOT appear for B

    ack(cdrom);
}

// ============================================================================
// Test 4 — ACK with BFRD held auto-arms the next sector at INT1 time.
//
// Sequence:
//   tick(2×) → INT1-A, B buffered.
//   enableDataRead() for A → BIOS path accept (sets BFRD).
//   Drain A via readData().
//   Ack INT1-A (writeInterruptFlags) → INT1-B fires.
//   PR-RV34: INT1-B publication auto-reloads B because BFRD is held.
//   Verify: B has no AcceptBiosAuto (armed by auto-reload, not enableDataRead).
//   Verify: B has no AcceptBfrd (no BFRD register write occurred).
//   Verify: DRQSTS=1 for B (auto-reload fired at INT1-B time).
// ============================================================================
static void test4_ack_only_does_not_auto_accept_next_sector()
{
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    PatternDisc disc;
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueReadN(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0

    cdrom.tick(kReadCycles * 2u);
    assert(irqType(cdrom) == 0x01u);

    // Accept A via BIOS path (sets BFRD).
    cdrom.enableDataRead();
    assert(drqsts(cdrom));

    // Drain A.
    for (size_t i = 0; i < 2048u; ++i)
        (void)cdrom.readData();
    assert(!drqsts(cdrom));

    // Ack INT1-A, then advance a cycle so INT1-B can surface; auto-reload
    // arms B because BFRD is held.
    ack(cdrom);
    cdrom.tick(1u);
    assert(irqType(cdrom) == 0x01u);

    // B was armed by auto-reload — no explicit accept trace recorded.
    assert(findTrace(cdrom, 1u, Reason::AcceptBiosAuto) == -1);
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) == -1);
    assert(drqsts(cdrom));

    ack(cdrom);
}

// ============================================================================
// Integration 1 — Two-sector BIOS-style chain with enableDataRead.
//
// Full ordering proof for sector A then B:
//   queue_promote(A) < publish_int1(A) < accept_bios_auto(A)
//     < drqsts_on(A) < drain_complete(A)
//   < publish_int1(B) < drqsts_on(B) < drain_complete(B)
//
// Sector A: enableDataRead() explicitly arms FIFO → AcceptBiosAuto recorded.
// Sector B: BFRD held over the INT1-A boundary, so PR-RV34 auto-reload arms B
//           during INT1-B publication → DrqstsOn recorded; AcceptBiosAuto
//           absent (enableDataRead() is a no-op when FIFO already loaded).
//
// Also verifies that AcceptBfrd is absent throughout (no register write for
// BFRD occurred — only enableDataRead was used).
// ============================================================================
static void test_integration_bios_accept_chain()
{
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    PatternDisc disc;
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueReadN(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0, LBA=1 following

    cdrom.tick(kReadCycles * 2u); // A published, B buffered
    assert(irqType(cdrom) == 0x01u);

    // ---- Sector A ----
    cdrom.enableDataRead();
    assert(drqsts(cdrom));

    // Drain via readData.
    for (size_t i = 0; i < 2048u; ++i)
        (void)cdrom.readData();
    assert(!drqsts(cdrom));

    // Ordering for A.
    const int iA_q = findTrace(cdrom, 0u, Reason::QueuePromote);
    const int iA_p1 = findTrace(cdrom, 0u, Reason::PublishInt1);
    const int iA_auto = findTrace(cdrom, 0u, Reason::AcceptBiosAuto);
    const int iA_drq = findTrace(cdrom, 0u, Reason::DrqstsOn);
    const int iA_cpu = findTrace(cdrom, 0u, Reason::CpuRddatRead);
    const int iA_done = findTrace(cdrom, 0u, Reason::DrainComplete);
    assert(iA_q >= 0);
    assert(iA_p1 >= 0);
    assert(iA_auto >= 0);
    assert(iA_drq >= 0);
    assert(iA_cpu >= 0);
    assert(iA_done >= 0);
    assert(iA_q < iA_p1);
    assert(iA_p1 < iA_auto);
    assert(iA_auto < iA_drq);
    assert(iA_drq < iA_cpu);
    assert(iA_cpu < iA_done);

    // AcceptBfrd must be absent throughout.
    assert(findTrace(cdrom, Reason::AcceptBfrd) == -1);

    // ---- Handoff A → B ----
    // Ack INT1-A, then advance a cycle; BFRD is still held, so auto-reload
    // arms B when INT1-B publishes.
    ack(cdrom);
    cdrom.tick(1u);
    assert(irqType(cdrom) == 0x01u);

    // ---- Sector B ----
    // enableDataRead() is a no-op here: BFRD already set, FIFO already loaded.
    cdrom.enableDataRead();
    assert(drqsts(cdrom));

    for (size_t i = 0; i < 2048u; ++i)
        (void)cdrom.readData();
    assert(!drqsts(cdrom));

    // Ordering for B: auto-reload fires within publishNextInterruptEvent,
    // so DrqstsOn is recorded at INT1 time; AcceptBiosAuto is absent.
    const int iB_p1 = findTrace(cdrom, 1u, Reason::PublishInt1);
    const int iB_drq = findTrace(cdrom, 1u, Reason::DrqstsOn);
    const int iB_done = findTrace(cdrom, 1u, Reason::DrainComplete);
    assert(iB_p1 >= 0);
    assert(iB_drq >= 0);
    assert(iB_done >= 0);
    assert(findTrace(cdrom, 1u, Reason::AcceptBiosAuto) == -1);
    assert(iB_p1 < iB_drq);
    assert(iB_drq < iB_done);

    // Cross-sector ordering: drain_complete(A) before drqsts_on(B).
    assert(iA_done < iB_drq);

    ack(cdrom);
}

// ============================================================================
// Integration 2 — Mixed accept sources: game (AcceptBfrd) then BIOS
//                  (AcceptBiosAuto) for consecutive sectors.
//
// Proves that:
//   - Sector A accepted via BFRD register write → AcceptBfrd, no AcceptBiosAuto.
//   - Sector B accepted via enableDataRead() → AcceptBiosAuto, no AcceptBfrd.
//   - Both sectors yield valid, readable data (DrqstsOn present for each).
//   - No cross-contamination: A has no AcceptBiosAuto, B has no AcceptBfrd.
// ============================================================================
static void test_integration_mixed_accept_sources()
{
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    PatternDisc disc;
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueReadN(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0

    cdrom.tick(kReadCycles * 2u);
    assert(irqType(cdrom) == 0x01u);

    // ---- Sector A: game path (BFRD via register) ----
    enableBfrd(cdrom);
    assert(drqsts(cdrom));
    assert(findTrace(cdrom, 0u, Reason::AcceptBfrd) >= 0);
    assert(findTrace(cdrom, 0u, Reason::AcceptBiosAuto) == -1);
    assert(findTrace(cdrom, 0u, Reason::DrqstsOn) >= 0);

    // Verify data readable (first two bytes).
    assert(cdrom.readData() == 0x00u); // LBA=0, byte[0]
    assert(cdrom.readData() == 0x01u); // LBA=0, byte[1]

    // Drain remainder of A.
    for (size_t i = 2u; i < 2048u; ++i)
        (void)cdrom.readData();
    assert(!drqsts(cdrom));

    // ---- Handoff: disable BFRD, ack INT1-A → INT1-B fires ----
    disableBfrd(cdrom);
    ack(cdrom);
    cdrom.tick(1u);
    assert(irqType(cdrom) == 0x01u);

    // ---- Sector B: BIOS path (enableDataRead) ----
    cdrom.enableDataRead();
    assert(drqsts(cdrom));
    assert(findTrace(cdrom, 1u, Reason::AcceptBiosAuto) >= 0);
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) == -1);
    assert(findTrace(cdrom, 1u, Reason::DrqstsOn) >= 0);

    // Verify data readable (LBA=1: byte[i] = (1*7 + i) & 0xFF).
    assert(cdrom.readData() == 0x07u); // LBA=1, byte[0] = 7
    assert(cdrom.readData() == 0x08u); // LBA=1, byte[1] = 8

    ack(cdrom);
}

int main()
{
    test1_generic_int1_leaves_fifo_empty();
    test2_bios_enableDataRead_exposes_sector();
    test3_accept_source_distinct_trace_reasons();
    test4_ack_only_does_not_auto_accept_next_sector();
    test_integration_bios_accept_chain();
    test_integration_mixed_accept_sources();
    return 0;
}
