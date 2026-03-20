#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"
#include "psxrecomp/runtime/kernel_events.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <memory>
#include <string>

namespace
{
using psxrecomp::u32;
using psxrecomp::u8;
using psxrecomp::runtime::EventMode;
using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::PsxSystem;
namespace EventClass = psxrecomp::runtime::EventClass;
namespace EventSpec = psxrecomp::runtime::EventSpec;

constexpr u32 kReadCycles = 451584u;
constexpr u32 kDataEndCallback = 0x80012000u;

class FiniteDisc final : public psxrecomp::runtime::Disc
{
  public:
    explicit FiniteDisc(u32 sectorCount) : m_sectorCount(sectorCount) {}

    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        if (lba >= m_sectorCount)
        {
            return false;
        }
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = static_cast<u8>((lba * 19u + static_cast<u32>(i)) & 0xFFu);
        }
        return true;
    }

    bool readRawSector2352(u32 lba, std::span<u8, 2352> out) override
    {
        if (lba >= m_sectorCount)
        {
            return false;
        }
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = static_cast<u8>((0x40u + lba + static_cast<u32>(i)) & 0xFFu);
        }
        return true;
    }

    u32 userSectorCount() const override
    {
        return m_sectorCount;
    }

  private:
    u32 m_sectorCount = 0;
};

u8 irqType(const psxrecomp::runtime::Cdrom& cdrom)
{
    return static_cast<u8>(cdrom.readInterruptFlags() & 0x07u);
}

void ackCdrom(psxrecomp::runtime::Cdrom& cdrom)
{
    while ((cdrom.readStatus() & 0x20u) != 0u)
    {
        (void)cdrom.readResponse();
    }
    cdrom.writeInterruptFlags(0x1Fu);
}

void issueSetloc(psxrecomp::runtime::Cdrom& cdrom, u8 mm, u8 ss, u8 ff)
{
    cdrom.writeParam(mm);
    cdrom.writeParam(ss);
    cdrom.writeParam(ff);
    cdrom.writeCommand(0x02);
    assert(irqType(cdrom) == 0x03);
    ackCdrom(cdrom);
}

void issueRead(psxrecomp::runtime::Cdrom& cdrom, u8 command)
{
    cdrom.writeCommand(command);
    assert(irqType(cdrom) == 0x03);
    ackCdrom(cdrom);
}

void enableBufferRead(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0u, 0u);
    cdrom.writeReg(3u, 0x80u);
}

void assertContains(const std::string& text, const std::string& needle)
{
    assert(text.find(needle) != std::string::npos);
}

void callA0(PsxSystem& system, u32 funcId, u32* regs)
{
    regs[9] = funcId;
    system.callBiosVector(0xA0, regs, 32);
}

bool pumpUntilCdromIrq(PsxSystem& system, u32 maxTicks = 5000)
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

void initSystemWithDisc(PsxSystem& system, const std::shared_ptr<psxrecomp::runtime::Disc>& disc)
{
    system.setDisc(disc);
    assert(system.initialize());
    system.interrupts().writeMask(system.interrupts().readMask() |
                                  static_cast<u32>(InterruptLine::Cdrom));

    u32 regs[32] = {};
    callA0(system, 0x54, regs);
    assert(regs[2] == 1);
    ackCdrom(system.cdrom());
    assert(pumpUntilCdromIrq(system, 500));
    ackCdrom(system.cdrom());
}

void testFiniteReadInt4LifecycleSummary()
{
    FiniteDisc disc(1);
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueRead(cdrom, 0x06);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    ackCdrom(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x04);
    (void)cdrom.readResponse();
    ackCdrom(cdrom);

    const std::string summary = cdrom.formatIrqLifecycleSummary();
    assertContains(summary,
                   "INT4 publish_gen=  1 queued=  1 published=  1 acked=  1 deasserted=  1");
    assertContains(summary, "INT4 published: 1");
    assertContains(summary, "INT4 acked: 1");
    assertContains(summary, "INT4 redispatched without new publish: 0");
    assertContains(summary, "INT4 HCLRCTL cleared active: yes");
    assertContains(summary, "top-level CD IRQ deassert after INT4 ack: yes");
}

void testInt1HandshakeSummary()
{
    FiniteDisc disc(2);
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueRead(cdrom, 0x06);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    enableBufferRead(cdrom);
    (void)cdrom.readDma();
    ackCdrom(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);

    const std::string summary = cdrom.formatIrqLifecycleSummary();
    const std::string payloadSummary = cdrom.formatCpuPayloadSummary();
    assertContains(summary,
                   "gen=1 lba=0 publish_bfrd=low bfrd_rose=yes accept=yes accepted=0 dma=yes "
                   "acked=yes deassert=yes");
    assertContains(summary,
                   "gen=2 lba=1 publish_bfrd=high bfrd_rose=no accept=yes accepted=1 dma=no "
                   "acked=no deassert=no");
    assertContains(summary, "first_unacked_int1_lba: 1");
    assertContains(summary,
                   "first_unacked_int1_handshake: publish_bfrd=high bfrd_rose=no accept=yes "
                   "dma=no ack=no top_level_cd_line_deassert=no");
    assertContains(payloadSummary, "int1_publish_lba=0 accepted_lba=0");
    assertContains(payloadSummary, "int1_publish_lba=1 accepted_lba=1");
}

void testStuckUnackedInt4ReportsRedispatch()
{
    PsxSystem system;
    auto disc = std::make_shared<FiniteDisc>(1);
    initSystemWithDisc(system, disc);
    system.cdrom().writeInterruptEnable(0x1Fu);

    u32 callbackCount = 0;
    system.setCallbackInvoker(
        [&callbackCount](u32 address) -> u32
        {
            if (address == kDataEndCallback)
            {
                ++callbackCount;
            }
            return 0;
        });

    const u32 dataEndEvent = system.events().openEvent(EventClass::Cdrom, EventSpec::DataEnd,
                                                       EventMode::Callback, kDataEndCallback);
    assert(dataEndEvent != 0xFFFFFFFFu);
    system.events().enableEvent(dataEndEvent);

    issueSetloc(system.cdrom(), 0x00, 0x02, 0x00);
    issueRead(system.cdrom(), 0x06);

    assert(pumpUntilCdromIrq(system, 5000));
    assert(irqType(system.cdrom()) == 0x01);
    ackCdrom(system.cdrom());

    assert(pumpUntilCdromIrq(system, 5000));
    assert(irqType(system.cdrom()) == 0x04);
    assert(callbackCount == 1u);

    system.serviceInterrupts();
    system.serviceInterrupts();
    assert(callbackCount == 1u);

    const std::string summary = system.cdrom().formatIrqLifecycleSummary();
    assertContains(summary, "INT4 published: 1");
    assertContains(summary, "INT4 acked: 0");
    assertContains(summary, "INT4 redispatched without new publish: 0");
    assertContains(summary, "INT4 HCLRCTL cleared active: no");

    ackCdrom(system.cdrom());
}

void testPublishAckRepublishDeliversOncePerGeneration()
{
    PsxSystem system;
    auto disc = std::make_shared<FiniteDisc>(2);
    initSystemWithDisc(system, disc);
    system.cdrom().writeInterruptEnable(0x1Fu);

    u32 callbackCount = 0;
    system.setCallbackInvoker(
        [&callbackCount](u32 address) -> u32
        {
            if (address == kDataEndCallback)
            {
                ++callbackCount;
            }
            return 0;
        });

    const u32 dataEndEvent = system.events().openEvent(EventClass::Cdrom, EventSpec::DataEnd,
                                                       EventMode::Callback, kDataEndCallback);
    assert(dataEndEvent != 0xFFFFFFFFu);
    system.events().enableEvent(dataEndEvent);

    issueSetloc(system.cdrom(), 0x00, 0x02, 0x00);
    issueRead(system.cdrom(), 0x06);

    assert(pumpUntilCdromIrq(system, 5000));
    assert(irqType(system.cdrom()) == 0x01);
    ackCdrom(system.cdrom());

    assert(pumpUntilCdromIrq(system, 5000));
    assert(irqType(system.cdrom()) == 0x01);
    ackCdrom(system.cdrom());

    assert(pumpUntilCdromIrq(system, 5000));
    assert(irqType(system.cdrom()) == 0x04);
    assert(callbackCount == 1u);
    system.serviceInterrupts();
    assert(callbackCount == 1u);
    ackCdrom(system.cdrom());

    issueSetloc(system.cdrom(), 0x00, 0x02, 0x00);
    issueRead(system.cdrom(), 0x06);

    assert(pumpUntilCdromIrq(system, 5000));
    assert(irqType(system.cdrom()) == 0x01);
    ackCdrom(system.cdrom());

    assert(pumpUntilCdromIrq(system, 5000));
    assert(irqType(system.cdrom()) == 0x01);
    ackCdrom(system.cdrom());

    assert(pumpUntilCdromIrq(system, 5000));
    assert(irqType(system.cdrom()) == 0x04);
    assert(callbackCount == 2u);
    system.serviceInterrupts();
    assert(callbackCount == 2u);
    ackCdrom(system.cdrom());

    const std::string summary = system.cdrom().formatIrqLifecycleSummary();
    assertContains(summary, "INT4 published: 2");
    assertContains(summary, "INT4 acked: 2");
    assertContains(summary, "INT4 redispatched without new publish: 0");
}
} // namespace

int main()
{
    testFiniteReadInt4LifecycleSummary();
    testInt1HandshakeSummary();
    testStuckUnackedInt4ReportsRedispatch();
    testPublishAckRepublishDeliversOncePerGeneration();
    return 0;
}
