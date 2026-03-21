// PR-RV21c: Per-sector CDROM host handoff — explicit phase-trace verification.
//
// Each test drives the CDROM through a scripted sector-access sequence and
// then inspects the rolling SectorPhaseTrace ring to prove that transitions
// fired in the correct order and with the correct LBA.
//
// The three host-visible states under test:
//   buffered  — sector is in m_bufferedReadSectors; NOT yet observable by software.
//   published — INT1 published; m_activeSector advanced; software can observe the IRQ.
//   accepted  — BFRD 0→1 transition; m_activeSector loaded into data FIFO; DRQSTS=1.
//
// Key invariants proven here:
//   • publish_int1 must precede accept_bfrd in the per-sector trace.
//   • No drqsts_on fires before the corresponding accept_bfrd.
//   • drain_complete for sector A does not promote sector B to accepted.
//   • DMA3 reads obey exactly the same host-side gate as CPU RDDAT reads.
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
// PatternDisc: sector N has byte[i] = (N * 13 + i) & 0xFF.
// ---------------------------------------------------------------------------
class PatternDisc final : public psxrecomp::runtime::Disc
{
  public:
    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<u8>((lba * 13u + static_cast<u32>(i)) & 0xFFu);
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
// Helpers shared across all tests.
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
    c.writeInterruptFlags(0x07u);
}

void readAndAck(psxrecomp::runtime::Cdrom& c)
{
    while ((c.readStatus() & (1u << 5u)) != 0u)
        (void)c.readResponse();
    ack(c);
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
    readAndAck(c);
    c.writeCommand(0x06u); // ReadN
    assert(irqType(c) == 0x03u);
    readAndAck(c);
}

// Scan the phase-trace ring for the first occurrence matching lba + reason.
// Returns the logical index (0 = oldest) or -1 if not found.
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

// Scan for any occurrence of reason (any LBA).
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
// Test 1 — Sector A requires INT1 before BFRD can expose data.
//
// Sequence:
//   Setloc + ReadN → INT3 ack
//   tick(1×) → INT1 published for LBA=0
//   Verify: trace has publish_int1 before any accept_bfrd.
//   Verify: no drqsts_on fires before accept_bfrd.
//   enableBfrd → accept_bfrd fires, followed by drqsts_on.
//   Verify: accept_bfrd and drqsts_on appear, in that order, after publish_int1.
// ============================================================================
static void test1_int1_before_bfrd()
{
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    PatternDisc disc;
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueReadN(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0

    // ---- Before INT1 fires ----
    // At this point: trace has publish_int3 (×2 for Setloc+ReadN commands),
    // hclrctl_ack (×2), and queue_promote might appear if ReadN immediately
    // buffered a sector.  No publish_int1 yet.
    assert(findTrace(cdrom, Reason::PublishInt1) == -1);

    // Tick one sector interval: INT1 fires and LBA‑0 moves to m_activeSector.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01u);

    const int idxInt1 = findTrace(cdrom, 0u, Reason::PublishInt1);
    assert(idxInt1 >= 0); // publish_int1 must have fired

    // No accept_bfrd yet (BFRD has not been written).
    assert(findTrace(cdrom, Reason::AcceptBfrd) == -1);
    assert(findTrace(cdrom, Reason::DrqstsOn) == -1);
    assert(!drqsts(cdrom));

    // ---- ARM BFRD ----
    enableBfrd(cdrom);
    assert(drqsts(cdrom));

    const int idxAccept = findTrace(cdrom, 0u, Reason::AcceptBfrd);
    const int idxDrq = findTrace(cdrom, 0u, Reason::DrqstsOn);
    assert(idxAccept > idxInt1); // accept_bfrd must follow publish_int1
    assert(idxDrq > idxAccept);  // drqsts_on must follow accept_bfrd
    assert(idxDrq > idxInt1);    // no drqsts_on fired before publish_int1 was followed by accept

    readAndAck(cdrom);
}

// ============================================================================
// Test 2 — Draining sector A does not expose sector B.
//
// Sequence:
//   tick(2×) → INT1-A published, B buffered in m_bufferedReadSectors.
//   enableBfrd → accept_bfrd + drqsts_on for LBA=0.
//   Drain all 2048 bytes of A via readData().
//   Verify: drain_complete fires for LBA=0.
//   Verify: NO accept_bfrd fires for LBA=1.
//   Verify: NO drqsts_on fires for LBA=1.
//   Verify: DRQSTS=0 after drain.
// ============================================================================
static void test2_drain_A_does_not_expose_B()
{
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    PatternDisc disc;
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueReadN(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0

    cdrom.tick(kReadCycles * 2u); // A published, B buffered
    assert(irqType(cdrom) == 0x01u);

    enableBfrd(cdrom);

    // Verify sector B is known to be in the queue (queue_promote for LBA=1).
    assert(findTrace(cdrom, 1u, Reason::QueuePromote) >= 0);
    // But NOT yet accepted.
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) == -1);
    assert(findTrace(cdrom, 1u, Reason::DrqstsOn) == -1);

    // Drain all of sector A.
    for (size_t i = 0; i < 2048u; ++i)
        (void)cdrom.readData();

    // drain_complete for A must have fired.
    assert(findTrace(cdrom, 0u, Reason::DrainComplete) >= 0);
    assert(!drqsts(cdrom));

    // Sector B must still NOT be accepted — no accept_bfrd or drqsts_on.
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) == -1);
    assert(findTrace(cdrom, 1u, Reason::DrqstsOn) == -1);

    readAndAck(cdrom); // ack INT1-A; cleanup
}

// ============================================================================
// Test 3 — Sector B requires its own INT1 + BFRD.
//
// Sequence:
//   tick(2×), BFRD, drain A, disableBfrd, ack INT1-A → INT1-B fires.
//   Before BFRD for B: trace shows publish_int1 for LBA=1, but no accept_bfrd.
//   enableBfrd for B: accept_bfrd fires for LBA=1, drqsts_on fires for LBA=1.
//   Verify ordering: publish_int1(B) < accept_bfrd(B).
// ============================================================================
static void test3_B_needs_own_int1_and_bfrd()
{
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    PatternDisc disc;
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueReadN(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0

    cdrom.tick(kReadCycles * 2u);
    assert(irqType(cdrom) == 0x01u);
    enableBfrd(cdrom);
    for (size_t i = 0; i < 2048u; ++i)
        (void)cdrom.readData();
    disableBfrd(cdrom);

    // Ack INT1-A, then advance a cycle so INT1-B can surface.
    readAndAck(cdrom);
    cdrom.tick(1u);
    assert(irqType(cdrom) == 0x01u);

    // publish_int1 for LBA=1 must be in trace; no accept_bfrd for LBA=1 yet.
    const int idxInt1B = findTrace(cdrom, 1u, Reason::PublishInt1);
    assert(idxInt1B >= 0);
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) == -1);
    assert(findTrace(cdrom, 1u, Reason::DrqstsOn) == -1);
    assert(!drqsts(cdrom));

    // ARM BFRD for sector B.
    enableBfrd(cdrom);
    assert(drqsts(cdrom));

    const int idxAcceptB = findTrace(cdrom, 1u, Reason::AcceptBfrd);
    const int idxDrqB = findTrace(cdrom, 1u, Reason::DrqstsOn);
    assert(idxAcceptB > idxInt1B); // accept_bfrd(B) after publish_int1(B)
    assert(idxDrqB >= idxAcceptB); // drqsts_on(B) at or after accept_bfrd(B)

    readAndAck(cdrom);
}

// ============================================================================
// Test 4 — DMA3 cannot bypass the host-side INT1+BFRD gate.
//
// Sequence:
//   tick(2×) → INT1-A, B buffered.
//   enableBfrd → LBA=0 accepted.
//   Drain all 512 words via readDma() → dma3_read fires for LBA=0,
//   then drain_complete fires for LBA=0.
//   Extra readDma() calls with BFRD=1 and empty FIFO must NOT fire
//   dma3_read nor drqsts_on for LBA=1.
//   Verify: no accept_bfrd or drqsts_on for LBA=1 until after ack + BFRD.
// ============================================================================
static void test4_dma3_obeys_phase_gate()
{
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    PatternDisc disc;
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueReadN(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0

    cdrom.tick(kReadCycles * 2u);
    assert(irqType(cdrom) == 0x01u);

    enableBfrd(cdrom);

    // Drain sector A via DMA3.
    for (size_t w = 0; w < 512u; ++w)
        (void)cdrom.readDma();

    // dma3_read fired for LBA=0.
    assert(findTrace(cdrom, 0u, Reason::Dma3Read) >= 0);
    // drain_complete fired for LBA=0.
    assert(findTrace(cdrom, 0u, Reason::DrainComplete) >= 0);
    assert(!drqsts(cdrom));

    // Extra DMA3 reads with an empty FIFO must NOT promote sector B.
    for (size_t extra = 0; extra < 8u; ++extra)
        (void)cdrom.readDma();

    // Still no accept_bfrd, drqsts_on, or dma3_read for LBA=1.
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) == -1);
    assert(findTrace(cdrom, 1u, Reason::DrqstsOn) == -1);
    assert(findTrace(cdrom, 1u, Reason::Dma3Read) == -1);

    // Proper handoff: disable BFRD, ack INT1-A, then advance a cycle.
    disableBfrd(cdrom);
    readAndAck(cdrom);
    cdrom.tick(1u);
    assert(irqType(cdrom) == 0x01u);
    assert(findTrace(cdrom, 1u, Reason::PublishInt1) >= 0);
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) == -1); // still not accepted

    // ARM BFRD → sector B accepted.
    enableBfrd(cdrom);
    assert(drqsts(cdrom));
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) >= 0);
    assert(findTrace(cdrom, 1u, Reason::DrqstsOn) >= 0);

    // DMA3 reads from B now fire dma3_read for LBA=1.
    (void)cdrom.readDma();
    assert(findTrace(cdrom, 1u, Reason::Dma3Read) >= 0);

    readAndAck(cdrom);
}

// ============================================================================
// Integration — Two-sector ReadN: phase-trace proves per-sector protocol.
//
// The trace must show the complete evidence chain for both sectors:
//   LBA=0: queue_promote → publish_int1 → accept_bfrd → drqsts_on
//          → cpu_rddat_read → drain_complete
//   LBA=1: queue_promote → (hclrctl_ack between sectors) → publish_int1
//          → accept_bfrd → drqsts_on → dma3_read → drain_complete
//
// This integration additionally confirms that:
//   • accept_bfrd for LBA=1 never appears before drain_complete for LBA=0.
//   • At no point does accepted_lba advance without a fresh publish_int1.
// ============================================================================
static void test_integration_two_sector_chain()
{
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    PatternDisc disc;
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueReadN(cdrom, 0x00u, 0x02u, 0x00u); // LBA=0, LBA=1 will be sector B

    cdrom.tick(kReadCycles * 2u); // A and B buffered; INT1-A visible
    assert(irqType(cdrom) == 0x01u);

    // ---- Sector A ----
    enableBfrd(cdrom);
    assert(drqsts(cdrom));

    // Drain via readData (CPU path).
    for (size_t i = 0; i < 2048u; ++i)
        (void)cdrom.readData();

    assert(!drqsts(cdrom));
    assert(findTrace(cdrom, 0u, Reason::DrainComplete) >= 0);
    assert(findTrace(cdrom, 0u, Reason::CpuRddatRead) >= 0);

    // Sector B must NOT yet be accepted.
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) == -1);

    // Check ordering in the trace up to this point.
    const int iA_q = findTrace(cdrom, 0u, Reason::QueuePromote);
    const int iA_p1 = findTrace(cdrom, 0u, Reason::PublishInt1);
    const int iA_acc = findTrace(cdrom, 0u, Reason::AcceptBfrd);
    const int iA_drq = findTrace(cdrom, 0u, Reason::DrqstsOn);
    const int iA_cpu = findTrace(cdrom, 0u, Reason::CpuRddatRead);
    const int iA_done = findTrace(cdrom, 0u, Reason::DrainComplete);
    assert(iA_q < iA_p1);
    assert(iA_p1 < iA_acc);
    assert(iA_acc < iA_drq);
    assert(iA_drq < iA_cpu);  // cpu_rddat_read must follow drqsts_on
    assert(iA_cpu < iA_done); // drain_complete is the last event for A

    // ---- Sector A → B handoff ----
    disableBfrd(cdrom);
    readAndAck(cdrom);
    cdrom.tick(1u);
    assert(irqType(cdrom) == 0x01u);

    // accept_bfrd for B must NOT have fired yet (no BFRD write since ack).
    assert(findTrace(cdrom, 1u, Reason::AcceptBfrd) == -1);

    // ---- Sector B ----
    enableBfrd(cdrom);
    assert(drqsts(cdrom));

    // Drain via readDma (DMA3 path).
    for (size_t w = 0; w < 512u; ++w)
        (void)cdrom.readDma();

    assert(!drqsts(cdrom));

    // Check full ordering for sector B.
    const int iB_q = findTrace(cdrom, 1u, Reason::QueuePromote);
    const int iB_p1 = findTrace(cdrom, 1u, Reason::PublishInt1);
    const int iB_acc = findTrace(cdrom, 1u, Reason::AcceptBfrd);
    const int iB_drq = findTrace(cdrom, 1u, Reason::DrqstsOn);
    const int iB_dma = findTrace(cdrom, 1u, Reason::Dma3Read);
    const int iB_done = findTrace(cdrom, 1u, Reason::DrainComplete);
    assert(iB_q >= 0);
    assert(iB_p1 >= 0);
    assert(iB_q < iB_p1);
    assert(iB_p1 < iB_acc);
    assert(iB_acc < iB_drq);
    assert(iB_drq < iB_dma);  // dma3_read must follow drqsts_on
    assert(iB_dma < iB_done); // drain_complete is last for B

    // Fundamental ordering guarantee: drain_complete(A) < accept_bfrd(B).
    assert(iA_done < iB_acc);

    // accept_bfrd for B must come after publish_int1 for B.
    assert(iA_done < iB_p1); // (already implied but be explicit)

    readAndAck(cdrom);
}

int main()
{
    test1_int1_before_bfrd();
    test2_drain_A_does_not_expose_B();
    test3_B_needs_own_int1_and_bfrd();
    test4_dma3_obeys_phase_gate();
    test_integration_two_sector_chain();
    return 0;
}
