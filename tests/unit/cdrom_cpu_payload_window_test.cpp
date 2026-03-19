#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <array>
#include <cassert>
#include <span>
#include <string>

namespace
{
using psxrecomp::u32;
using psxrecomp::u8;

constexpr u32 kReadCycles = 451584u;
constexpr size_t kSectorCount = 40u;

class RollingPayloadDisc final : public psxrecomp::runtime::Disc
{
  public:
    RollingPayloadDisc()
    {
        for (size_t lba = 0; lba < kSectorCount; ++lba)
        {
            auto& sector = m_raw[lba];
            sector.fill(0);
            sector[15] = 0x02; // Mode2
            sector[16] = 0x01;
            sector[17] = static_cast<u8>(lba & 0x1Fu);
            sector[18] = 0x20; // Form2, non-audio/non-realtime -> CPU data path.
            sector[19] = 0x00;
            sector[20] = sector[16];
            sector[21] = sector[17];
            sector[22] = sector[18];
            sector[23] = sector[19];
            for (size_t i = 24; i < sector.size(); ++i)
            {
                sector[i] = static_cast<u8>((lba + i) & 0xFFu);
            }
        }
    }

    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        if (lba >= kSectorCount)
        {
            return false;
        }
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = m_raw[lba][24 + i];
        }
        return true;
    }

    bool readRawSector2352(u32 lba, std::span<u8, 2352> out) override
    {
        if (lba >= kSectorCount)
        {
            return false;
        }
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = m_raw[lba][i];
        }
        return true;
    }

    u32 userSectorCount() const override
    {
        return static_cast<u32>(kSectorCount);
    }

  private:
    std::array<std::array<u8, 2352>, kSectorCount> m_raw{};
};

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

void disableBfrd(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0, 0);
    cdrom.writeReg(3, 0x00);
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

void issueReadS(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeCommand(0x1B);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
}

void issuePause(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeCommand(0x09);
    assert(irqType(cdrom) == 0x03);
    readAndAck(cdrom);
    assert(irqType(cdrom) == 0x02);
    readAndAck(cdrom);
}

void readHeaderAndPayloadViaDma(psxrecomp::runtime::Cdrom& cdrom)
{
    for (int i = 0; i < 3; ++i)
    {
        (void)cdrom.readDma();
    }
    for (int i = 0; i < 512; ++i)
    {
        (void)cdrom.readDma();
    }
}

bool hasPhase(const psxrecomp::runtime::Cdrom& cdrom, u32 lba,
              psxrecomp::runtime::Cdrom::SectorPhaseReason reason)
{
    for (size_t i = 0; i < cdrom.phaseTraceCount(); ++i)
    {
        const auto entry = cdrom.phaseTraceEntry(i);
        if (entry.lba == lba && entry.reason == reason)
        {
            return true;
        }
    }
    return false;
}

void testRollingSummaryRetainsLast32AcceptedSectors()
{
    RollingPayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x60); // XA stream + raw 2340 payloads.
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    for (u32 lba = 0; lba < kSectorCount; ++lba)
    {
        enableBfrd(cdrom);
        readHeaderAndPayloadViaDma(cdrom);
        readAndAck(cdrom);

        if (lba + 1u < kSectorCount)
        {
            cdrom.tick(kReadCycles);
            assert(irqType(cdrom) == 0x01);
            disableBfrd(cdrom);
            enableBfrd(cdrom);
        }
    }

    const std::string summary = cdrom.formatCpuPayloadSummary();
    assert(summary.find("sectors_recorded:    32") != std::string::npos);
    assert(summary.find("retention:           last 32 accepted/read sectors") != std::string::npos);
    assert(summary.find("int1_publish_lba_range: 8..39") != std::string::npos);
    assert(summary.find("accepted_lba_range:     8..39") != std::string::npos);
    assert(summary.find("dma_only=32") != std::string::npos);
    assert(summary.find("int1_publish_lba=39 accepted_lba=39") != std::string::npos);
    assert(summary.find("dma_bytes=2060") != std::string::npos);
    assert(summary.find("mode=raw2340") != std::string::npos);
    assert(summary.find("superseded=yes") != std::string::npos);
    assert(summary.find("superseded=no") != std::string::npos);
}

void testSupersededSectorReportsFinalOffsetAtNextPublish()
{
    RollingPayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x60);
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);
    readHeaderAndPayloadViaDma(cdrom);
    readAndAck(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    const std::string summary = cdrom.formatCpuPayloadSummary();
    assert(summary.find("sectors_recorded:    1") != std::string::npos);
    assert(summary.find("int1_publish_lba_range: 0..0") != std::string::npos);
    assert(summary.find("accepted_lba_range:     0..0") != std::string::npos);
    assert(summary.find("int1_publish_lba=0 accepted_lba=0") != std::string::npos);
    assert(summary.find("superseded=yes") != std::string::npos);
    assert(summary.find("next_publish=2060") != std::string::npos);
    assert(summary.find("final=2060") != std::string::npos);
    assert(summary.find("unread=280") != std::string::npos);
}

void testPublishedAndDrainingSectorStateStayDistinct()
{
    RollingPayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x60);
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadS(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);
    readHeaderAndPayloadViaDma(cdrom);
    readAndAck(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    const auto snapshot = cdrom.debugSnapshot();
    assert(snapshot.publishedSectorValid);
    assert(snapshot.drainingSectorValid);
    assert(snapshot.publishedLba == 1u);
    assert(snapshot.drainingLba == 0u);

    const std::string beforeMoreDma = cdrom.formatCpuPayloadSummary();
    assert(beforeMoreDma.find("int1_publish_lba=0 accepted_lba=0") != std::string::npos);
    assert(beforeMoreDma.find("accepted_lba=1") == std::string::npos);
    assert(beforeMoreDma.find("next_publish=2060") != std::string::npos);
    assert(beforeMoreDma.find("final=2060") != std::string::npos);
    assert(hasPhase(cdrom, 0u, psxrecomp::runtime::Cdrom::SectorPhaseReason::Dma3Read));
    assert(!hasPhase(cdrom, 1u, psxrecomp::runtime::Cdrom::SectorPhaseReason::Dma3Read));

    (void)cdrom.readDma();

    const std::string afterMoreDma = cdrom.formatCpuPayloadSummary();
    assert(afterMoreDma.find("int1_publish_lba=0 accepted_lba=0") != std::string::npos);
    assert(afterMoreDma.find("accepted_lba=1") == std::string::npos);
    assert(afterMoreDma.find("next_publish=2060") != std::string::npos);
    assert(afterMoreDma.find("final=2064") != std::string::npos);
}

void testReadRestartKeepsPriorAcceptedWindow()
{
    RollingPayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x60);
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    for (u32 lba = 0; lba < 3u; ++lba)
    {
        enableBfrd(cdrom);
        readHeaderAndPayloadViaDma(cdrom);
        readAndAck(cdrom);

        if (lba + 1u < 3u)
        {
            cdrom.tick(kReadCycles);
            assert(irqType(cdrom) == 0x01);
            disableBfrd(cdrom);
        }
    }

    issuePause(cdrom);

    issueSetloc(cdrom, 0x00, 0x02, 0x0A);
    issueReadN(cdrom);
    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);

    const std::string summary = cdrom.formatCpuPayloadSummary();
    assert(summary.find("sectors_recorded:    4") != std::string::npos);
    assert(summary.find("int1_publish_lba=0 accepted_lba=0") != std::string::npos);
    assert(summary.find("int1_publish_lba=2 accepted_lba=2") != std::string::npos);
    assert(summary.find("int1_publish_lba=10 accepted_lba=10") != std::string::npos);
    assert(summary.find("dma_only=3") != std::string::npos);
    assert(summary.find("unread=1") != std::string::npos);
}

void testNonXaReadStillCapturesPayloadWindow()
{
    RollingPayloadDisc disc;
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1F);

    issueSetmode(cdrom, 0x20); // raw 2340, XA streaming disabled
    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBfrd(cdrom);
    readHeaderAndPayloadViaDma(cdrom);

    const std::string summary = cdrom.formatCpuPayloadSummary();
    assert(summary.find("sectors_recorded:    1") != std::string::npos);
    assert(summary.find("int1_publish_lba=0 accepted_lba=0") != std::string::npos);
    assert(summary.find("mode=raw2340") != std::string::npos);
    assert(summary.find("dma_bytes=2060") != std::string::npos);
}
} // namespace

int main()
{
    testRollingSummaryRetainsLast32AcceptedSectors();
    testSupersededSectorReportsFinalOffsetAtNextPublish();
    testPublishedAndDrainingSectorStateStayDistinct();
    testReadRestartKeepsPriorAcceptedWindow();
    testNonXaReadStillCapturesPayloadWindow();
    return 0;
}
