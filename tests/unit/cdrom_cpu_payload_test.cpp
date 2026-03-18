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
        if (lba >= 3u) return false;
        for (size_t i = 0; i < out.size(); ++i) out[i] = m_raw[lba][24 + i];
        return true;
    }

    bool readRawSector2352(u32 lba, std::span<u8, 2352> out) override
    {
        if (lba >= 3u) return false;
        for (size_t i = 0; i < out.size(); ++i) out[i] = m_raw[lba][i];
        return true;
    }

    u32 userSectorCount() const override { return 3u; }

    // Expose raw sector data for test verification.
    u8 rawByte(u32 lba, size_t offset) const { return m_raw[lba][offset]; }

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

void ack(psxrecomp::runtime::Cdrom& cdrom) { cdrom.writeInterruptFlags(0x07); }

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
    for (int i = 0; i < 581; ++i) (void)cdrom.readDma();
    while ((cdrom.readStatus() & (1u << 5)) != 0u) (void)cdrom.readResponse();
    ack(cdrom);

    // Trigger a second sector (LBA 1 Mode1) so the Form2 record is finalized.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    const std::string summary = cdrom.formatCpuPayloadSummary();

    // Payload mode must be form2-2324.
    assert(summary.find("payload_mode:        form2-2324") != std::string::npos);
    // First recorded sector must have size 2324.
    assert(summary.find("size=2324") != std::string::npos);
    // First byte of LBA 0 payload is raw[24] = (0 + 0xA0) & 0xFF = 0xa0.
    assert(summary.find("first16:  a0") != std::string::npos);
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
    for (int i = 0; i < 512; ++i) (void)cdrom.readDma();
    while ((cdrom.readStatus() & (1u << 5)) != 0u) (void)cdrom.readResponse();
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

int main()
{
    testForm2NonAudioRecordedAs2324();
    testMode1RecordedAsUser2048();
    testXaAdpcmNotRecorded();
    return 0;
}
