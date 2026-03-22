#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"
#include "psxrecomp/runtime/kernel_events.h"
#include "psxrecomp/runtime/psx_system.h"

#include <array>
#include <cassert>
#include <cstring>
#include <memory>

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
constexpr u32 kBiosA0BasePc = 0x80017600u;
constexpr u32 kCdromPumpPc = 0x80017700u;

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
            out[i] = static_cast<u8>((lba * 17u + static_cast<u32>(i)) & 0xFFu);
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
            out[i] = static_cast<u8>((0x80u + lba + static_cast<u32>(i)) & 0xFFu);
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
    while ((cdrom.readStatus() & (1u << 5)) != 0u)
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

void testFiniteReadProducesInt4AndStops(u8 command)
{
    FiniteDisc disc(2);
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueSetloc(cdrom, 0x00, 0x02, 0x00);
    issueRead(cdrom, command);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    ackCdrom(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    ackCdrom(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x04);
    assert(!cdrom.debugSnapshot().readActive);
    const u8 stat = cdrom.readResponse();
    assert((stat & (1u << 5)) == 0u);
    cdrom.writeInterruptFlags(0x1Fu);

    cdrom.tick(kReadCycles * 2u);
    assert(irqType(cdrom) == 0x00);
    assert(!cdrom.debugSnapshot().readActive);
}

void callA0(PsxSystem& system, u32 funcId, u32* regs)
{
    system.observeProgramCounter(kBiosA0BasePc + (funcId << 2));
    regs[9] = funcId;
    system.callBiosVector(0xA0, regs, 32);
}

bool pumpUntilCdromIrq(PsxSystem& system, u32 maxTicks = 5000)
{
    for (u32 i = 0; i < maxTicks; ++i)
    {
        system.tickCpuCycles(2048);
        system.observeProgramCounter(kCdromPumpPc + (i << 2));
        if (system.cdrom().hasIrqRequest())
        {
            system.serviceInterrupts();
            return true;
        }
        system.serviceInterrupts();
    }
    return false;
}

void ackSystemCdromIrq(PsxSystem& system)
{
    ackCdrom(system.cdrom());
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
    ackSystemCdromIrq(system);
    assert(pumpUntilCdromIrq(system, 500));
    ackSystemCdromIrq(system);
}

void testPsxSystemDeliversDataEndEvent()
{
    PsxSystem system;
    auto disc = std::make_shared<FiniteDisc>(1);
    initSystemWithDisc(system, disc);
    system.cdrom().writeInterruptEnable(0x1Fu);

    const u32 dataReadyEvent = system.events().openEvent(EventClass::Cdrom, EventSpec::DataReady,
                                                         EventMode::NoCallback, 0);
    const u32 dataEndEvent =
        system.events().openEvent(EventClass::Cdrom, EventSpec::DataEnd, EventMode::NoCallback, 0);
    assert(dataReadyEvent != 0xFFFFFFFFu);
    assert(dataEndEvent != 0xFFFFFFFFu);
    system.events().enableEvent(dataReadyEvent);
    system.events().enableEvent(dataEndEvent);

    system.cdrom().writeParam(0x00);
    system.cdrom().writeParam(0x02);
    system.cdrom().writeParam(0x00);
    system.cdrom().writeCommand(0x02);
    assert(system.cdrom().hasIrqRequest());
    system.observeProgramCounter(0x80017800u);
    system.serviceInterrupts();
    ackSystemCdromIrq(system);

    system.cdrom().writeCommand(0x06);
    assert(system.cdrom().hasIrqRequest());
    system.observeProgramCounter(0x80017804u);
    system.serviceInterrupts();
    ackSystemCdromIrq(system);

    assert(pumpUntilCdromIrq(system, 5000));
    assert(system.events().isEventDelivered(dataReadyEvent));
    assert(!system.events().isEventDelivered(dataEndEvent));
    assert(system.events().testEvent(dataReadyEvent));
    ackSystemCdromIrq(system);

    assert(pumpUntilCdromIrq(system, 5000));
    assert(!system.cdrom().debugSnapshot().readActive);
    assert(system.events().isEventDelivered(dataEndEvent));
    assert(system.events().testEvent(dataEndEvent));
    ackSystemCdromIrq(system);

    system.tickCpuCycles(kReadCycles * 2u);
    system.observeProgramCounter(0x80017808u);
    system.serviceInterrupts();
    assert(!system.events().isEventDelivered(dataReadyEvent));
    assert(!system.cdrom().hasIrqRequest());
}
} // namespace

int main()
{
    testFiniteReadProducesInt4AndStops(0x06);
    testFiniteReadProducesInt4AndStops(0x1B);
    testPsxSystemDeliversDataEndEvent();
    return 0;
}
