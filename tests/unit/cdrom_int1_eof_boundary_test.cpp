#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/disc.h"

#include <array>
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
using psxrecomp::u32;
using psxrecomp::u8;

constexpr u32 kReadCycles = 451584u;
constexpr u8 kSetmodeXa = 0x40u;
constexpr u8 kXaSubmodeData = 0x08u;
constexpr u8 kXaSubmodeEor = 0x01u;
constexpr u8 kXaSubmodeEof = 0x80u;
constexpr const char* kGateEnv = "PSXRECOMP_EXPERIMENT_GATE_EOF_BOUNDARY_INT1";

std::array<u8, 2352> makeDataSector(u8 file, u8 channel, u8 submode, u8 coding, u8 fill)
{
    std::array<u8, 2352> raw{};
    raw[15] = 0x02;
    raw[16] = file;
    raw[17] = channel;
    raw[18] = submode;
    raw[19] = coding;
    raw[20] = raw[16];
    raw[21] = raw[17];
    raw[22] = raw[18];
    raw[23] = raw[19];
    for (size_t i = 24; i < raw.size(); ++i)
    {
        raw[i] = static_cast<u8>(fill + static_cast<u8>(i));
    }
    return raw;
}

class RawDisc final : public psxrecomp::runtime::Disc
{
  public:
    explicit RawDisc(std::vector<std::array<u8, 2352>> raw) : m_raw(std::move(raw)) {}

    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        if (lba >= m_raw.size())
        {
            return false;
        }
        const auto& raw = m_raw[lba];
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = raw[24 + i];
        }
        return true;
    }

    bool readRawSector2352(u32 lba, std::span<u8, 2352> out) override
    {
        if (lba >= m_raw.size())
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
        return static_cast<u32>(m_raw.size());
    }

  private:
    std::vector<std::array<u8, 2352>> m_raw;
};

class ScopedEnv final
{
  public:
    ScopedEnv(const char* key, const char* value) : m_key(key)
    {
        if (const char* old = std::getenv(key))
        {
            m_oldValue = old;
            m_hadOldValue = true;
        }
        assert(::setenv(key, value, 1) == 0);
    }

    ~ScopedEnv()
    {
        if (m_hadOldValue)
        {
            assert(::setenv(m_key.c_str(), m_oldValue.c_str(), 1) == 0);
        }
        else
        {
            assert(::unsetenv(m_key.c_str()) == 0);
        }
    }

  private:
    std::string m_key;
    std::string m_oldValue;
    bool m_hadOldValue = false;
};

u8 irqType(const psxrecomp::runtime::Cdrom& cdrom)
{
    return static_cast<u8>(cdrom.readInterruptFlags() & 0x07u);
}

void enableBufferRead(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0u, 0u);
    cdrom.writeReg(3u, 0x80u);
}

void disableBufferRead(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeReg(0u, 0u);
    cdrom.writeReg(3u, 0x00u);
}

void ack(psxrecomp::runtime::Cdrom& cdrom)
{
    while ((cdrom.readStatus() & 0x20u) != 0u)
    {
        (void)cdrom.readResponse();
    }
    cdrom.writeInterruptFlags(0x1Fu);
}

void assertContains(const std::string& text, const std::string& needle)
{
    assert(text.find(needle) != std::string::npos);
}

void issueXaReadN(psxrecomp::runtime::Cdrom& cdrom)
{
    cdrom.writeParam(kSetmodeXa);
    cdrom.writeCommand(0x0E);
    assert(irqType(cdrom) == 0x03);
    ack(cdrom);

    cdrom.writeParam(0x00);
    cdrom.writeParam(0x02);
    cdrom.writeParam(0x00);
    cdrom.writeCommand(0x02);
    assert(irqType(cdrom) == 0x03);
    ack(cdrom);

    cdrom.writeCommand(0x06);
    assert(irqType(cdrom) == 0x03);
    ack(cdrom);
}

void testBoundarySummary()
{
    RawDisc disc(
        {makeDataSector(0x03, 0x07, kXaSubmodeData | kXaSubmodeEor | kXaSubmodeEof, 0x11, 0x20),
         makeDataSector(0x03, 0x07, kXaSubmodeData, 0x22, 0x40)});
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueXaReadN(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    cdrom.enableDataRead();
    (void)cdrom.readData();
    ack(cdrom);

    cdrom.tick(kReadCycles);
    assert(irqType(cdrom) == 0x01);
    cdrom.enableDataRead();
    (void)cdrom.readData();

    const std::string summary = cdrom.formatIrqLifecycleSummary();
    assertContains(summary,
                   "gen=1 lba=0 publish_bfrd=low bfrd_rose=no accept=yes accepted=0 dma=no "
                   "dma_bytes=0 dma_dst=none hclrctl=yes acked=yes deassert=yes");
    assertContains(summary, "submode=0x89 coding=0x11 eor=yes eof=yes");
    assertContains(summary,
                   "gen=2 lba=1 publish_bfrd=high bfrd_rose=no accept=yes accepted=1 dma=no "
                   "dma_bytes=0 dma_dst=none hclrctl=no acked=no deassert=no");
    assertContains(summary, "submode=0x08 coding=0x22 eor=no eof=no");
    assertContains(summary, "last_acked_int1_lba: 0");
    assertContains(summary, "last_acked_submode: 0x89");
    assertContains(summary, "first_unacked_int1_lba: 1");
    assertContains(summary, "first_unacked_submode: 0x08");
    assertContains(summary, "same_file_channel = yes");
    assertContains(summary, "stalled_on_sector_after_eof = yes");
}

void testGatedModeSuppressesImmediateBoundaryPublication()
{
    ScopedEnv gateEnv(kGateEnv, "1");
    RawDisc disc(
        {makeDataSector(0x01, 0x01, kXaSubmodeData | kXaSubmodeEor | kXaSubmodeEof, 0x10, 0x20),
         makeDataSector(0x02, 0x03, kXaSubmodeData, 0x11, 0x40)});
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueXaReadN(cdrom);

    cdrom.tick(kReadCycles * 2u);
    assert(irqType(cdrom) == 0x01u);

    enableBufferRead(cdrom);
    for (size_t i = 0; i < 2048u; ++i)
    {
        (void)cdrom.readData();
    }
    ack(cdrom);
    cdrom.tick(1u);
    assert(irqType(cdrom) == 0u);

    disableBufferRead(cdrom);
    enableBufferRead(cdrom);
    cdrom.tick(1u);
    assert(irqType(cdrom) == 0x01u);
    assert(cdrom.readData() == makeDataSector(0x02, 0x03, kXaSubmodeData, 0x11, 0x40)[24]);
}

void testGatedModeKeepsNonEofSequentialStreaming()
{
    ScopedEnv gateEnv(kGateEnv, "1");
    RawDisc disc({makeDataSector(0x04, 0x05, kXaSubmodeData, 0x12, 0x10),
                  makeDataSector(0x04, 0x05, kXaSubmodeData, 0x13, 0x30)});
    psxrecomp::runtime::Cdrom cdrom;
    cdrom.reset();
    cdrom.setDiscBackend(&disc);
    cdrom.writeInterruptEnable(0x1Fu);

    issueXaReadN(cdrom);

    cdrom.tick(kReadCycles * 2u);
    assert(irqType(cdrom) == 0x01u);

    enableBufferRead(cdrom);
    for (size_t i = 0; i < 2048u; ++i)
    {
        (void)cdrom.readData();
    }
    ack(cdrom);
    cdrom.tick(1u);
    assert(irqType(cdrom) == 0x01u);
    assert(cdrom.readData() == makeDataSector(0x04, 0x05, kXaSubmodeData, 0x13, 0x30)[24]);
}
} // namespace

int main()
{
    testBoundarySummary();
    testGatedModeSuppressesImmediateBoundaryPublication();
    testGatedModeKeepsNonEofSequentialStreaming();
    return 0;
}
