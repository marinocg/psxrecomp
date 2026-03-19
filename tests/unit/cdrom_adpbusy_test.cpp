// PR-RV26: ADPBUSY (HSTS bit 2) disc-XA playback busy state.
//
// Verifies the ADPBUSY model in readStatus():
//
//   1. XA sector consumed → ADPBUSY (bit 2) set in readStatus() while readActive.
//   2. Non-XA sector (Mode1) in XA-streaming mode → ADPBUSY never set.
//   3. ADPBUSY implicitly cleared when read stops (readActive=false after Pause).
//
// PSX-SPX: 1F801800h bit 2 = ADPBUSY (XA-ADPCM FIFO non-empty during playback).
// http://problemkaputt.de/psx-spx.htm#cdromcontrollerioports

#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
using psxrecomp::u32;
using psxrecomp::u8;

constexpr u32 kReadCycles = 451584u;
constexpr u8 kAdpbusyBit = 1u << 2;

// ---------------------------------------------------------------------------
// AdpbusyDisc: LBA 0 = Mode2/Form2/audio+realtime (XA-ADPCM),
//              LBA 1 = Mode1 plain data.
// ---------------------------------------------------------------------------
class AdpbusyDisc final : public psxrecomp::runtime::Disc
{
  public:
    AdpbusyDisc()
    {
        // LBA 0: XA-ADPCM
        m_xa.fill(0);
        m_xa[15] = 0x02;
        m_xa[16] = 0x01; // file
        m_xa[17] = 0x01; // channel
        m_xa[18] = 0x64; // form2 | realtime | audio
        m_xa[19] = 0x01; // stereo 4-bit
        m_xa[20] = m_xa[16];
        m_xa[21] = m_xa[17];
        m_xa[22] = m_xa[18];
        m_xa[23] = m_xa[19];

        // LBA 1: Mode1 data
        m_mode1.fill(0);
        m_mode1[15] = 0x01;
        for (size_t i = 16; i < 2352; ++i)
        {
            m_mode1[i] = static_cast<u8>(i & 0xFFu);
        }
    }

    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        if (lba == 0)
        {
            for (size_t i = 0; i < out.size(); ++i) out[i] = m_xa[24 + i];
            return true;
        }
        if (lba == 1)
        {
            for (size_t i = 0; i < out.size(); ++i) out[i] = m_mode1[24 + i];
            return true;
        }
        return false;
    }

    bool readRawSector2352(u32 lba, std::span<u8, 2352> out) override
    {
        if (lba == 0)
        {
            for (size_t i = 0; i < out.size(); ++i) out[i] = m_xa[i];
            return true;
        }
        if (lba == 1)
        {
            for (size_t i = 0; i < out.size(); ++i) out[i] = m_mode1[i];
            return true;
        }
        return false;
    }

    u32 userSectorCount() const override { return 2u; }

  private:
    std::array<u8, 2352> m_xa;
    std::array<u8, 2352> m_mode1;
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
// Test 1: XA sector consumed → ADPBUSY set while read is active.
// ---------------------------------------------------------------------------
void testAdpbusySetAfterXaSector()
{
    AdpbusyDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40); // XA streaming
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);

    // Before any sector: ADPBUSY must be clear.
    assert((cdrom.readStatus() & kAdpbusyBit) == 0u);

    // Tick through one XA-ADPCM sector.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x00); // INT1 suppressed

    // ADPBUSY (bit 2) must now be set (readActive still true).
    assert((cdrom.readStatus() & kAdpbusyBit) != 0u);
}

// ---------------------------------------------------------------------------
// Test 2: Mode1 sector in XA-streaming mode → ADPBUSY never set.
// ---------------------------------------------------------------------------
void testAdpbusyNotSetForMode1Sector()
{
    AdpbusyDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40); // XA streaming
    // Seek to LBA 1 (Mode1 data).
    issueSetloc(cdrom, 0x00, 0x02, 0x01);
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01); // INT1 fires for Mode1

    // ADPBUSY must be clear — no XA sector was consumed.
    assert((cdrom.readStatus() & kAdpbusyBit) == 0u);

    // Ack INT1.
    while ((cdrom.readStatus() & (1u << 5)) != 0u) (void)cdrom.readResponse();
    ack(cdrom);
}

// ---------------------------------------------------------------------------
// Test 3: ADPBUSY implicitly clears when readActive=false (after Pause).
// ---------------------------------------------------------------------------
void testAdpbusyClearedOnPause()
{
    AdpbusyDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x00);
    assert((cdrom.readStatus() & kAdpbusyBit) != 0u); // set after XA

    // Issue Pause (0x09) to stop reading.
    cdrom.writeCommand(0x09);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
    // Second response for Pause.
    cdrom.tick(100u);
    if (irqType(cdrom) != 0u) readAndAck(cdrom);

    // ADPBUSY must now be clear (readActive=false).
    assert((cdrom.readStatus() & kAdpbusyBit) == 0u);
}

// ---------------------------------------------------------------------------
// Test 4: ADPBUSY explicitly cleared on Stop and does not re-appear at
// the start of a subsequent ReadN (before any XA sector arrives).
// This is the PR-RV30 sticky-latch regression test: previously
// m_xaPlaybackBusy was never cleared on Stop, so seekActive=true at the
// new ReadN was enough to show ADPBUSY before the first XA sector.
// ---------------------------------------------------------------------------
void testAdpbusyResetBetweenStreams()
{
    AdpbusyDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    // First stream: consume one XA-ADPCM sector.
    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x00);             // INT1 suppressed (XA)
    assert((cdrom.readStatus() & kAdpbusyBit) != 0u); // ADPBUSY set

    // Stop: ADPBUSY must clear explicitly.
    cdrom.writeCommand(0x08);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
    if (irqType(cdrom) != 0u) readAndAck(cdrom); // INT2
    assert((cdrom.readStatus() & kAdpbusyBit) == 0u);

    // Second stream: ADPBUSY must stay clear before any XA sector arrives.
    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);
    // seekActive=true here — with the old latch ADPBUSY would already be set.
    assert((cdrom.readStatus() & kAdpbusyBit) == 0u);

    // Confirm ADPBUSY rises again once the new XA sector arrives.
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x00);
    assert((cdrom.readStatus() & kAdpbusyBit) != 0u);
}

// ---------------------------------------------------------------------------
// Test 5: formatAdpbusyLifecycleSummary() reports rose/fell/sectors fields
// after a complete play → pause lifecycle.
// ---------------------------------------------------------------------------
void testAdpbusyLifecycleSummary()
{
    AdpbusyDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);
    cdrom.tick(kReadCycles); // one XA sector

    // Summary while busy.
    std::string s = cdrom.formatAdpbusyLifecycleSummary();
    assert(s.find("adpbusy_now:") != std::string::npos);
    assert(s.find("rose_at_lba:") != std::string::npos);
    assert(s.find("sectors_busy:") != std::string::npos);
    assert(s.find("still busy") != std::string::npos);

    // Pause → fell_at_lba appears.
    cdrom.writeCommand(0x09);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
    if (irqType(cdrom) != 0u) readAndAck(cdrom);

    s = cdrom.formatAdpbusyLifecycleSummary();
    assert(s.find("fell_at_lba:") != std::string::npos);
}

int main()
{
    testAdpbusySetAfterXaSector();
    testAdpbusyNotSetForMode1Sector();
    testAdpbusyClearedOnPause();
    testAdpbusyResetBetweenStreams();
    testAdpbusyLifecycleSummary();
    return 0;
}
