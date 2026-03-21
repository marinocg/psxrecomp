// PR-RV25: Disc XA sink consumption and observability.
//
// Verifies that the built-in stub sink (no audio callback) tracks XA sector
// consumption metrics and does not expose data to the CPU path.  Three cases:
//
//   1. Stub sink: no setXaAudioSink() → xa_sink:stub, xa_consumed:1 sector.
//   2. Real sink: setXaAudioSink() provided → xa_sink:real, callback invoked.
//   3. CPU-path isolation: after xa_audio_deliver, STATUS_DATA_READY is clear
//      and readDma() returns zero.
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
// XaSinkDisc: single Mode2/Form2/audio+realtime sector at LBA 0.
// ---------------------------------------------------------------------------
class XaSinkDisc final : public psxrecomp::runtime::Disc
{
  public:
    XaSinkDisc()
    {
        m_raw.fill(0);
        m_raw[15] = 0x02; // Mode 2
        m_raw[16] = 0x01; // file
        m_raw[17] = 0x02; // channel
        m_raw[18] = 0x64; // form2(bit5) | realtime(bit6) | audio(bit2)
        m_raw[19] = 0x01; // stereo 4-bit
        m_raw[20] = m_raw[16];
        m_raw[21] = m_raw[17];
        m_raw[22] = m_raw[18];
        m_raw[23] = m_raw[19];
        for (size_t i = 0; i < 2324; ++i)
        {
            m_raw[24 + i] = static_cast<u8>(i & 0xFFu);
        }
    }

    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        if (lba != 0)
        {
            return false;
        }
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = m_raw[24 + i];
        }
        return true;
    }

    bool readRawSector2352(u32 lba, std::span<u8, 2352> out) override
    {
        if (lba != 0)
        {
            return false;
        }
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = m_raw[i];
        }
        return true;
    }

    u32 userSectorCount() const override
    {
        return 1u;
    }

  private:
    std::array<u8, 2352> m_raw;
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
} // namespace

// ---------------------------------------------------------------------------
// Test 1: Stub sink tracks consumed sectors.
//
// With no audio-sink callback set, the built-in stub records consumption.
// Summary must report xa_sink:stub and xa_consumed:1 sector.
// ---------------------------------------------------------------------------
void testStubSinkConsumedCount()
{
    XaSinkDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40); // XA streaming, no audio sink set
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x00); // INT1 suppressed

    const std::string summary = cdrom.formatXaClassificationSummary();
    assert(summary.find("xa_sink:          stub") != std::string::npos);
    assert(summary.find("xa_consumed:      1") != std::string::npos);
    assert(summary.find("last_coding=0x01") != std::string::npos);
    assert(summary.find("INT1_suppressed:  1") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 2: Real audio sink → summary labels it "real" and callback fires.
//
// When setXaAudioSink() is provided, the sink receives decoded PCM and
// the summary reports xa_sink:real.
// ---------------------------------------------------------------------------
void testRealSinkLabel()
{
    XaSinkDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    bool sinkCalled = false;
    cdrom.setXaAudioSink([&](const auto&) { sinkCalled = true; });

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x00);
    assert(sinkCalled);

    const std::string summary = cdrom.formatXaClassificationSummary();
    assert(summary.find("xa_sink:          real") != std::string::npos);
    assert(summary.find("xa_consumed:      1") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 3: XA delivery does not feed the CPU data path.
//
// After xa_audio_deliver: STATUS_DATA_READY (bit 6) is clear, and
// readDma() with BFRD=1 returns zero — the data FIFO is empty.
// ---------------------------------------------------------------------------
void testXaDeliveryDoesNotFeedCpuPath()
{
    XaSinkDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x40);
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x00);

    // DATA_READY (bit 6) must be clear — no CPU-visible sector queued.
    assert((cdrom.readStatus() & 0x40u) == 0u);

    // Enable buffer-read and confirm readDma() returns zero (FIFO empty).
    cdrom.writeReg(0, 0);
    cdrom.writeReg(3, 0x80); // BFRD=1
    assert(cdrom.readDma() == 0u);
}

int main()
{
    testStubSinkConsumedCount();
    testRealSinkLabel();
    testXaDeliveryDoesNotFeedCpuPath();
    return 0;
}
