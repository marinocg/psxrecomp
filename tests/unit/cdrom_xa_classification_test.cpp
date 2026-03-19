// PR-RV23: XA sector classification and delivery tracing.
//
// Verifies the three key classification rules per PSX-SPX:
//
//   1. XA-ADPCM sector (Mode2/Form2/audio+realtime) with XA streaming enabled
//      → xa_audio_deliver: decoded to SPU, INT1 suppressed.
//
//   2. Non-ADPCM sector (Mode2/Form2 without audio+realtime, or plain Mode1)
//      with XA streaming enabled → cpu_data_deliver: INT1 fires.
//
//   3. XA-ADPCM sector with XA filter enabled and file/channel mismatch
//      → filter_reject: sector scanned past, no INT1 and no audio delivery.
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
using Reason = psxrecomp::runtime::Cdrom::SectorPhaseReason;

constexpr u32 kReadCycles = 451584u;

// ---------------------------------------------------------------------------
// Build a 2352-byte raw Mode2/Form2 sector with configurable subheader.
// ---------------------------------------------------------------------------
std::array<u8, 2352> makeRawXaSector(u8 file, u8 channel, u8 submode, u8 codingInfo)
{
    std::array<u8, 2352> raw{};
    raw[15] = 0x02; // Mode 2
    raw[16] = file;
    raw[17] = channel;
    raw[18] = submode;
    raw[19] = codingInfo;
    raw[20] = raw[16]; // duplicate subheader (required for valid decode)
    raw[21] = raw[17];
    raw[22] = raw[18];
    raw[23] = raw[19];
    for (size_t i = 0; i < 2324; ++i)
    {
        raw[24 + i] = static_cast<u8>(i & 0xFFu);
    }
    return raw;
}

// ---------------------------------------------------------------------------
// XaClassifyDisc: a small disc whose sectors exercise each classification path.
//
//   LBA 0: Mode2/Form2/audio+realtime (XA-ADPCM) file=1 ch=2
//   LBA 1: Mode2/Form2 — form2 but NOT audio+realtime (video-like)
//   LBA 2: Mode2/Form2/audio+realtime (XA-ADPCM) file=3 ch=4 (mismatching filter)
// ---------------------------------------------------------------------------
class XaClassifyDisc final : public psxrecomp::runtime::Disc
{
  public:
    XaClassifyDisc()
    {
        // submode 0x64 = form2(bit5) | realtime(bit6) | audio(bit2)
        m_sectors[0] = makeRawXaSector(0x01, 0x02, 0x64, 0x01);
        // submode 0x20 = form2(bit5) only — NOT audio or realtime
        m_sectors[1] = makeRawXaSector(0x01, 0x02, 0x20, 0x00);
        // submode 0x64 — ADPCM but different file/channel for filter test
        m_sectors[2] = makeRawXaSector(0x03, 0x04, 0x64, 0x01);
    }

    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        if (lba >= m_sectors.size())
        {
            return false;
        }
        const auto& raw = m_sectors[lba];
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = raw[24 + i];
        }
        return true;
    }

    bool readRawSector2352(u32 lba, std::span<u8, 2352> out) override
    {
        if (lba >= m_sectors.size())
        {
            return false;
        }
        const auto& raw = m_sectors[lba];
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = raw[i];
        }
        return true;
    }

    u32 userSectorCount() const override
    {
        return static_cast<u32>(m_sectors.size());
    }

  private:
    std::array<std::array<u8, 2352>, 3> m_sectors;
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

// ---------------------------------------------------------------------------
// Test 1: XA-ADPCM sector with XA streaming enabled → xa_audio_deliver.
//
// The sector is Mode2/Form2/audio+realtime (LBA 0).  With Setmode 0x40
// (XA_STREAM_ENABLE), it must be classified as xa_audio_deliver and must NOT
// generate an INT1 interrupt.
// ---------------------------------------------------------------------------
void testXaAdpcmSuppressesInt1()
{
    XaClassifyDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);            // XA streaming enable
    issueSetloc(cdrom, 0x00, 0x02, 0x00); // LBA=0 (ADPCM sector)
    issueReadN(cdrom);

    // One full cadence: LBA 0 is XA-ADPCM → xa_audio_deliver, no INT1.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x00);

    // Classification counters must confirm xa_audio_deliver.
    const std::string summary = cdrom.formatXaClassificationSummary();
    assert(summary.find("INT1_suppressed:  1") != std::string::npos);
    assert(summary.find("INT1_count:       0") != std::string::npos);

    // Phase trace ring must contain an XaAudioDeliver entry.
    bool foundXaAudio = false;
    for (size_t i = 0; i < cdrom.phaseTraceCount(); ++i)
    {
        if (cdrom.phaseTraceEntry(i).reason == Reason::XaAudioDeliver)
        {
            foundXaAudio = true;
        }
    }
    assert(foundXaAudio);
}

// ---------------------------------------------------------------------------
// Test 2: Non-ADPCM Form2 sector with XA streaming enabled → cpu_data_deliver.
//
// LBA 1 is Mode2/Form2 but submode has only form2 bit set (not audio or
// realtime).  Even with XA streaming on, this sector must generate INT1 and
// be classified as submode_reject (Form2/non-ADPCM, INT1 fired).
// ---------------------------------------------------------------------------
void testNonAdpcmForm2GeneratesInt1()
{
    XaClassifyDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);            // XA streaming enable
    issueSetloc(cdrom, 0x00, 0x02, 0x01); // LBA=1 (Form2 but not ADPCM)
    issueReadN(cdrom);

    // One cadence: LBA 1 is Form2/non-ADPCM → submode_reject → INT1 fires.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    // INT1 was not suppressed.
    const std::string summary = cdrom.formatXaClassificationSummary();
    assert(summary.find("INT1_suppressed:  0") != std::string::npos);
    assert(summary.find("INT1_count:       1") != std::string::npos);

    // Phase trace ring must contain a SubmodeReject entry.
    bool foundSubmode = false;
    for (size_t i = 0; i < cdrom.phaseTraceCount(); ++i)
    {
        if (cdrom.phaseTraceEntry(i).reason == Reason::SubmodeReject)
        {
            foundSubmode = true;
        }
    }
    assert(foundSubmode);
}

// ---------------------------------------------------------------------------
// Test 3: XA-ADPCM with filter enabled and mismatching file/channel
//         → filter_reject: sector scanned past, no INT1 and no audio.
//
// LBA 2 is ADPCM (file=3, ch=4) but the filter is set to file=1, ch=2.
// The sector must be filter-rejected (scan continues).  Since XaClassifyDisc
// has no sector at LBA 3, the read returns false for that cadence tick.
// ---------------------------------------------------------------------------
void testFilterMismatchRejectsSector()
{
    XaClassifyDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x48); // XA streaming + XA filter enable
    // Setfilter file=1 ch=2 (does not match LBA 2's file=3 ch=4)
    cdrom.writeParam(0x01);
    cdrom.writeParam(0x02);
    cdrom.writeCommand(0x0D);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);

    issueSetloc(cdrom, 0x00, 0x02, 0x02); // LBA=2 (ADPCM, filter mismatch)
    issueReadN(cdrom);

    // One cadence: LBA 2 → filter_reject, scan tries LBA 3 which is past the
    // finite disc → no INT1 data-ready, stream completes with INT4/DataEnd.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x04);
    readAndAck(cdrom);

    // filter_reject counter must be non-zero; INT1 must not have fired.
    const std::string summary = cdrom.formatXaClassificationSummary();
    assert(summary.find("INT1_suppressed:  0") != std::string::npos);
    assert(summary.find("INT1_count:       0") != std::string::npos);

    bool foundFilterReject = false;
    for (size_t i = 0; i < cdrom.phaseTraceCount(); ++i)
    {
        if (cdrom.phaseTraceEntry(i).reason == Reason::FilterReject)
        {
            foundFilterReject = true;
        }
    }
    assert(foundFilterReject);
}
} // namespace

int main()
{
    testXaAdpcmSuppressesInt1();
    testNonAdpcmForm2GeneratesInt1();
    testFilterMismatchRejectsSector();
    return 0;
}
