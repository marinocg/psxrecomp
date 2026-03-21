// PR-RV28: Post-stream CPU payload tracer tests.
//
// Verifies that formatCpuPayloadSummary() captures the correct payload mode
// and byte content for each CPU-visible sector after an XA-enabled ReadS/ReadN.
//
// Test cases:
//
//   1. Form2 non-audio sector → recorded as form2-2324 (2324 bytes from raw[24]).
//   2. Mode1 / user-data sector → recorded as user2048 (2048 bytes from raw[24]).
//   3. XA-ADPCM sector → NOT recorded (audio-deliver path suppresses INT1 and
//      the sector never reaches publishNextInterruptEvent with a buffered sector).
//
// http://problemkaputt.de/psx-spx.htm#cdromcontrollerioports

#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

namespace
{
using psxrecomp::u32;
using psxrecomp::u8;

constexpr u32 kReadCycles = 451584u;

// ---------------------------------------------------------------------------
// PayloadDisc: three sectors with distinct types.
//   LBA 0 — Mode2/Form2 non-audio+non-realtime (submode_reject → CPU, 2324 bytes)
//   LBA 1 — Mode1 data (user2048)
//   LBA 2 — Mode2/Form2/audio+realtime XA-ADPCM (xa_audio_deliver, not CPU)
// ---------------------------------------------------------------------------
class PayloadDisc final : public psxrecomp::runtime::Disc
{
  public:
    PayloadDisc()
    {
        // LBA 0: Form2 but NOT audio+realtime — MDEC/video-style sector.
        // submode = form2 only (0x20), no audio or realtime bits.
        auto& s0 = m_raw[0];
        s0.fill(0);
        s0[15] = 0x02;
        s0[16] = 0x01; // file
        s0[17] = 0x02; // channel
        s0[18] = 0x20; // form2 only; no audio(0x04), no realtime(0x40)
        s0[19] = 0x00; // coding info
        s0[20] = s0[16];
        s0[21] = s0[17];
        s0[22] = s0[18];
        s0[23] = s0[19];
        for (size_t i = 0; i < 2324; ++i)
        {
            s0[24 + i] = static_cast<u8>((i + 0xA0u) & 0xFFu); // recognizable pattern
        }

        // LBA 1: Mode1 — plain user data.
        auto& s1 = m_raw[1];
        s1.fill(0);
        s1[15] = 0x01;
        for (size_t i = 16; i < 2352; ++i)
        {
            s1[i] = static_cast<u8>((i + 0xB0u) & 0xFFu);
        }

        // LBA 2: Mode2/Form2/audio+realtime — XA-ADPCM (delivered to audio, no INT1).
        auto& s2 = m_raw[2];
        s2.fill(0);
        s2[15] = 0x02;
        s2[16] = 0x01; // file
        s2[17] = 0x03; // channel
        s2[18] = 0x64; // form2(0x20) | realtime(0x40) | audio(0x04)
        s2[19] = 0x01; // stereo 4-bit
        s2[20] = s2[16];
        s2[21] = s2[17];
        s2[22] = s2[18];
        s2[23] = s2[19];
        for (size_t i = 0; i < 2324; ++i)
        {
            s2[24 + i] = static_cast<u8>((i + 0xC0u) & 0xFFu);
        }
    }

    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        if (lba >= 3u)
            return false;
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = m_raw[lba][24 + i];
        return true;
    }

    bool readRawSector2352(u32 lba, std::span<u8, 2352> out) override
    {
        if (lba >= 3u)
            return false;
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = m_raw[lba][i];
        return true;
    }

    u32 userSectorCount() const override
    {
        return 3u;
    }

    // Expose raw sector data for test verification.
    u8 rawByte(u32 lba, size_t offset) const
    {
        return m_raw[lba][offset];
    }

  private:
    std::array<std::array<u8, 2352>, 3> m_raw;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
u8 irqType(const psxrecomp::runtime::Cdrom& cdrom)
{
    return static_cast<u8>(cdrom.readInterruptFlags() & 0x07u);
}

void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeInterruptFlags(0x07);
}

void readAndAck(psxrecomp::runtime::Cdrom& cdrom)
{
    (void)cdrom.readResponse();
    ack(cdrom);
}

void enableBfrd(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0, 0);
    cdrom.writeReg(3, 0x80);
}

void issueSetmode(psxrecomp::runtime::Cdrom& cdrom, u8 mode)
{
    cdrom.writeParam(mode);
    cdrom.writeCommand(0x0E);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
}

void issueSetloc(psxrecomp::runtime::Cdrom& cdrom, u8 mm, u8 ss, u8 ff)
{
    cdrom.writeParam(mm);
    cdrom.writeParam(ss);
    cdrom.writeParam(ff);
    cdrom.writeCommand(0x02);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
}

void issueReadN(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeCommand(0x06);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
}
} // namespace

// ---------------------------------------------------------------------------
// Test 1: Form2 non-audio sector → recorded as form2-2324.
//
// Setmode 0x40 (XA streaming, no filter), ReadN starting at LBA 0.
// LBA 0 is Mode2/Form2 without audio or realtime bits → SubmodeReject → CPU
// data path → INT1 fires → 2324 bytes loaded into FIFO.
// Summary must report payload_mode:form2-2324, size=2324, and the first byte
// of the payload (raw[24] = 0xA0) matches.
// ---------------------------------------------------------------------------
void testForm2NonAudioRecordedAs2324()
{
    PayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40); // XA streaming
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);

    // Tick: LBA 0 (Form2 non-audio) → INT1.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    enableBfrd(cdrom);
    // Read enough DMA words to drain the sector (2324 bytes = 581 words).
    for (int i = 0; i < 581; ++i)
        (void)cdrom.readDma();
    readAndAck(cdrom);

    const std::string summary = cdrom.formatCpuPayloadSummary();

    // Payload mode must be form2-2324.
    assert(summary.find("payload_mode:        form2-2324") != std::string::npos);
    // First recorded sector must have size 2324.
    assert(summary.find("size=2324") != std::string::npos);
    // First byte of LBA 0 payload is raw[24] = (0 + 0xA0) & 0xFF = 0xa0.
    assert(summary.find("first16: a0") != std::string::npos);
    // DMA3 was used (581 reads).
    assert(summary.find("dma3=yes") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 2: Mode1 user-data sector → recorded as user2048.
//
// Setmode 0x40, ReadN at LBA 1 (Mode1). No XA subheader → CPU data path →
// 2048 bytes. Summary reports payload_mode:user2048, size=2048.
// ---------------------------------------------------------------------------
void testMode1RecordedAsUser2048()
{
    PayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x01); // LBA 1
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    enableBfrd(cdrom);
    for (int i = 0; i < 512; ++i)
        (void)cdrom.readDma();
    while ((cdrom.readStatus() & (1u << 5)) != 0u)
        (void)cdrom.readResponse();
    ack(cdrom);

    // Tick again for finalization (LBA 2 XA-ADPCM does NOT generate INT1,
    // so the record stays with dma3=? until we issue one more tick).
    // Use a second tick to try finalization via the XA sector:
    cdrom.tick(kReadCycles); // LBA 2 → xa_audio_deliver, no INT1

    const std::string summary = cdrom.formatCpuPayloadSummary();

    assert(summary.find("payload_mode:        user2048") != std::string::npos);
    assert(summary.find("size=2048") != std::string::npos);
    // DMA3 was used.
    assert(summary.find("dma3=yes") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 3: XA-ADPCM sector → NOT recorded in the CPU payload summary.
//
// Setmode 0x40, ReadN at LBA 2 (XA-ADPCM). The sector goes to xa_audio_deliver;
// INT1 is suppressed; no buffered sector reaches publishNextInterruptEvent.
// formatCpuPayloadSummary() must report 0 sectors recorded.
// ---------------------------------------------------------------------------
void testXaAdpcmNotRecorded()
{
    PayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x02); // LBA 2 (XA-ADPCM)
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x00); // INT1 suppressed

    const std::string summary = cdrom.formatCpuPayloadSummary();

    // No CPU sectors recorded — only the "no sectors" message.
    assert(summary.find("no post-stream CPU sectors recorded") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 4: CPU-only read — verify cpuFirstOffset, cpuBytesRead, dmaFirstOffset=none.
//
// ReadN at LBA 0 (Form2, 2324 bytes), BFRD, read 16 bytes via readData().
// Second sector (LBA 1 Mode1) fires INT1 to finalize LBA 0's record.
// Summary must show cpu_start=0, cpu_bytes=16, dma_start=none, final=16.
// ---------------------------------------------------------------------------
void testReadWindowCpuOnly()
{
    PayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA 0 (Form2, 2324 bytes)
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);

    for (int i = 0; i < 16; ++i)
        (void)cdrom.readData();

    while ((cdrom.readStatus() & (1u << 5)) != 0u)
        (void)cdrom.readResponse();
    ack(cdrom);

    // Second sector (LBA 1 Mode1) → INT1 → finalizes LBA 0 record.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    const std::string s = cdrom.formatCpuPayloadSummary();
    assert(s.find("cpu_start=0") != std::string::npos);
    assert(s.find("cpu_bytes=16") != std::string::npos);
    assert(s.find("dma_start=none") != std::string::npos);
    assert(s.find("dma_bytes=0") != std::string::npos);
    assert(s.find("final=16") != std::string::npos);
    assert(s.find("cpu_only=1") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 5: DMA-only read — verify dmaFirstOffset=0, dmaBytesRead=2324.
//
// ReadN at LBA 0 (Form2, 2324 bytes), BFRD, drain all via readDma().
// Second sector (LBA 1 Mode1) finalizes LBA 0's record.
// Summary must show cpu_start=none, dma_start=0, dma_bytes=2324, final=2324.
// ---------------------------------------------------------------------------
void testReadWindowDmaOnly()
{
    PayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA 0 (Form2, 2324 bytes)
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);

    // Drain all 2324 bytes via DMA (581 × 4-byte words).
    for (int i = 0; i < 581; ++i)
        (void)cdrom.readDma();

    while ((cdrom.readStatus() & (1u << 5)) != 0u)
        (void)cdrom.readResponse();
    ack(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    const std::string s = cdrom.formatCpuPayloadSummary();
    assert(s.find("cpu_start=none") != std::string::npos);
    assert(s.find("cpu_bytes=0") != std::string::npos);
    assert(s.find("dma_start=0") != std::string::npos);
    assert(s.find("dma_bytes=2324") != std::string::npos);
    assert(s.find("final=2324") != std::string::npos);
    assert(s.find("dma_only=1") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 6: Mixed read — CPU reads 4 bytes then DMA drains the rest.
//
// ReadN at LBA 0 (Form2, 2324 bytes), read 4 bytes via readData() then
// drain remaining 2320 bytes via readDma().  After finalization:
//   cpu_start=0, cpu_bytes=4, dma_start=4, dma_bytes=2320, final=2324.
// ---------------------------------------------------------------------------
void testReadWindowMixed()
{
    PayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA 0 (Form2, 2324 bytes)
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);

    // CPU reads first 4 bytes.
    for (int i = 0; i < 4; ++i)
        (void)cdrom.readData();
    // DMA drains remaining 2320 bytes (580 × 4-byte words).
    for (int i = 0; i < 580; ++i)
        (void)cdrom.readDma();

    while ((cdrom.readStatus() & (1u << 5)) != 0u)
        (void)cdrom.readResponse();
    ack(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    const std::string s = cdrom.formatCpuPayloadSummary();
    assert(s.find("cpu_start=0") != std::string::npos);
    assert(s.find("cpu_bytes=4") != std::string::npos);
    assert(s.find("dma_start=4") != std::string::npos);
    assert(s.find("dma_bytes=2320") != std::string::npos);
    assert(s.find("final=2324") != std::string::npos);
    assert(s.find("mixed=1") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 7: BFRD held + FIFO empty → new sector armed in FIFO at INT1.
//
// PR-RV34 fix: tick() no longer calls acceptBufferedReadSector before INT1.
// The auto-reload in publishNextInterruptEvent now fires when BFRD is held
// regardless of FIFO state, so a fully-drained FIFO arms the new sector.
// ---------------------------------------------------------------------------
void testBfrdHeldFifoEmptyArmedAtInt1()
{
    PayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA 0 (Form2, 2324 bytes)
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);
    for (int i = 0; i < 581; ++i)
        (void)cdrom.readDma(); // drain 2324 bytes
    assert(cdrom.debugSnapshot().dataFifoSize == 0);
    readAndAck(cdrom);

    // BFRD still held, FIFO empty.  Second sector (LBA 1 Mode1, 2048 bytes).
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    // Sector must be armed in FIFO immediately by publishNextInterruptEvent.
    assert(cdrom.debugSnapshot().dataFifoSize == 2048);
}

// ---------------------------------------------------------------------------
// Test 8: Sector must NOT be readable before its INT1 is published.
//
// With a previous INT1 still active (unacked), a new sector arriving in
// tick() must NOT load into the FIFO.  Only after the game acks the
// previous INT1 does publishNextInterruptEvent advance the sector and arm
// the FIFO for the new INT1.
// ---------------------------------------------------------------------------
void testSectorNotPreloadedBeforeInt1()
{
    PayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA 0
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);
    for (int i = 0; i < 581; ++i)
        (void)cdrom.readDma(); // drain LBA 0

    // Do NOT ack INT1.  Second sector (LBA 1) arrives but INT1 is blocked.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);                  // Previous INT1 still set.
    assert(cdrom.debugSnapshot().dataFifoSize == 0); // LBA 1 NOT preloaded.

    // Ack previous INT1, then advance a cycle so LBA 1's INT1 can publish.
    readAndAck(cdrom);
    cdrom.tick(1u);
    assert(irqType(cdrom) == 0x01);                     // LBA 1 INT1 now live.
    assert(cdrom.debugSnapshot().dataFifoSize == 2048); // LBA 1 armed.
}

// ---------------------------------------------------------------------------
// Test 9: Tracer records DMA reads on the second sector (PR-RV34 regression).
//
// Before the fix, tick() called acceptBufferedReadSector before publishing
// INT1 when BFRD was held and the FIFO was drained.  This emptied the
// buffer so publishNextInterruptEvent took the INT1-no-buffered path, which
// skipped snapshotCpuSector.  DMA reads on the second sector were invisible.
// After the fix, both sectors are snapshotted and dma_only=2.
// ---------------------------------------------------------------------------
void testTracerRecordsDmaOnSecondSector()
{
    PayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA 0 (Form2, 2324 bytes)
    issueReadN(cdrom);

    // LBA 0: INT1, drain via DMA, ack.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);
    for (int i = 0; i < 581; ++i)
        (void)cdrom.readDma();
    readAndAck(cdrom);

    // LBA 1 (Mode1, 2048 bytes): INT1 with BFRD held, FIFO armed at publish.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    for (int i = 0; i < 512; ++i)
        (void)cdrom.readDma(); // drain 2048 bytes

    const std::string s = cdrom.formatCpuPayloadSummary();
    // Both sectors must be recorded (snapshotCpuSector called for each).
    assert(s.find("sectors_recorded:    2") != std::string::npos);
    // Both sectors were DMA-only (no CPU reads).
    assert(s.find("dma_only=2") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 10: Split DMA window — two separate DMA bursts accumulate one pointer.
//
// Simulates the game's 12-byte header DMA followed by 2048-byte payload DMA
// (with a BFRD=1 no-op write between them, as observed in Reversi 2).
// Verifies that m_dataFifoConsumedBytes accumulates across both bursts so the
// tracer records dma_start=0, dma_bytes=2060, final=2060 (not 0).
// ---------------------------------------------------------------------------
void testSplitDmaWindow()
{
    PayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA 0 (Form2, 2324 bytes)
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);

    // Burst 1: 3 DMA words = 12 bytes (header read).
    for (int i = 0; i < 3; ++i)
        (void)cdrom.readDma();
    // BFRD=1 again: 1→1 no-op, FIFO pointer unchanged.
    enableBfrd(cdrom);
    // Burst 2: 512 DMA words = 2048 bytes (payload read).
    for (int i = 0; i < 512; ++i)
        (void)cdrom.readDma();

    readAndAck(cdrom); // LBA 0 finalized via INT1-no-buffered (finalOffset=2060).

    const std::string s = cdrom.formatCpuPayloadSummary();
    assert(s.find("dma_start=0") != std::string::npos);
    assert(s.find("dma_bytes=2060") != std::string::npos);
    assert(s.find("final=2060") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 11: FIFO non-empty at INT1 → readable block replaced (PR-RV55).
//
// When bytes remain unread at the next INT1 boundary and BFRD is still armed,
// the new INT1 becomes the new host-visible data phase immediately. Verifies:
//   - the unread remainder is discarded at the new INT1 boundary
//   - FIFO now exposes the next sector without requiring a BFRD toggle
// ---------------------------------------------------------------------------
void testFifoNonEmptyAtInt1ReplacesReadableBlock()
{
    PayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA 0 (Form2, 2324 bytes)
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom); // FIFO loaded: 2324 bytes.

    // Partial read: 3 DMA words = 12 bytes. 2312 bytes remain.
    for (int i = 0; i < 3; ++i)
        (void)cdrom.readDma();
    assert(cdrom.debugSnapshot().dataFifoSize == 2312);

    readAndAck(cdrom); // LBA 0 finalized; BFRD stays 1.

    // Second tick: LBA 1 INT1 fires with BFRD still held.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    assert(cdrom.debugSnapshot().dataFifoSize == 2048);
    const u32 expectedLba1Word = static_cast<u32>(disc.rawByte(1u, 24u)) |
                                 (static_cast<u32>(disc.rawByte(1u, 25u)) << 8) |
                                 (static_cast<u32>(disc.rawByte(1u, 26u)) << 16) |
                                 (static_cast<u32>(disc.rawByte(1u, 27u)) << 24);
    assert(cdrom.readDma() == expectedLba1Word);
}

int main()
{
    testForm2NonAudioRecordedAs2324();
    testMode1RecordedAsUser2048();
    testXaAdpcmNotRecorded();
    testReadWindowCpuOnly();
    testReadWindowDmaOnly();
    testReadWindowMixed();
    testBfrdHeldFifoEmptyArmedAtInt1();
    testSectorNotPreloadedBeforeInt1();
    testTracerRecordsDmaOnSecondSector();
    testSplitDmaWindow();
    testFifoNonEmptyAtInt1ReplacesReadableBlock();
    return 0;
}
