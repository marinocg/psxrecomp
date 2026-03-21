/**
 * @file bios_file_test.cpp
 * @brief Tests for the B0-vector BIOS file APIs backed by ISO 9660 disc data.
 */
#include "psxrecomp/runtime/disc.h"
#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using psxrecomp::u16;
using psxrecomp::u32;
using psxrecomp::u8;
using psxrecomp::runtime::PsxSystem;

namespace
{
constexpr size_t kSectorSize = 2048;
constexpr u32 kPvdSector = 16;
constexpr u32 kRootDirLba = 20;
constexpr u32 kSubdirLba = 21;
constexpr u32 kHelloLba = 30;
constexpr u32 kSystemCnfLba = 32;
constexpr u32 kNestedLba = 33;

void writeBothEndian16(u8* dst, u16 value)
{
    dst[0] = static_cast<u8>(value & 0xFFu);
    dst[1] = static_cast<u8>((value >> 8) & 0xFFu);
    dst[2] = static_cast<u8>((value >> 8) & 0xFFu);
    dst[3] = static_cast<u8>(value & 0xFFu);
}

void writeBothEndian32(u8* dst, u32 value)
{
    dst[0] = static_cast<u8>(value & 0xFFu);
    dst[1] = static_cast<u8>((value >> 8) & 0xFFu);
    dst[2] = static_cast<u8>((value >> 16) & 0xFFu);
    dst[3] = static_cast<u8>((value >> 24) & 0xFFu);
    dst[4] = static_cast<u8>((value >> 24) & 0xFFu);
    dst[5] = static_cast<u8>((value >> 16) & 0xFFu);
    dst[6] = static_cast<u8>((value >> 8) & 0xFFu);
    dst[7] = static_cast<u8>(value & 0xFFu);
}

std::vector<u8> makeDirectoryRecord(const std::vector<u8>& name, u32 lba, u32 size, u8 flags)
{
    const size_t padding = (name.size() % 2u) == 0u ? 1u : 0u;
    std::vector<u8> record(33u + name.size() + padding, 0u);
    record[0] = static_cast<u8>(record.size());
    writeBothEndian32(record.data() + 2, lba);
    writeBothEndian32(record.data() + 10, size);
    record[25] = flags;
    writeBothEndian16(record.data() + 28, 1u);
    record[32] = static_cast<u8>(name.size());
    std::copy(name.begin(), name.end(), record.begin() + 33);
    return record;
}

void appendRecord(std::array<u8, kSectorSize>& sector, size_t& offset, const std::vector<u8>& rec)
{
    assert(offset + rec.size() <= sector.size());
    std::copy(rec.begin(), rec.end(), sector.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += rec.size();
}

u32 readLe32(const u8* ptr)
{
    return static_cast<u32>(ptr[0]) | (static_cast<u32>(ptr[1]) << 8) |
           (static_cast<u32>(ptr[2]) << 16) | (static_cast<u32>(ptr[3]) << 24);
}

std::string readDirName(const u8* ram, u32 address)
{
    std::string out;
    for (size_t i = 0; i < 20 && ram[address + i] != 0; ++i)
    {
        out.push_back(static_cast<char>(ram[address + i]));
    }
    return out;
}

void writeCString(u8* ram, u32 address, const std::string& value)
{
    std::memcpy(ram + address, value.c_str(), value.size() + 1u);
}

void callB0(PsxSystem& system, u32 functionId, u32* regs)
{
    regs[9] = functionId;
    system.callBiosVector(0xB0, regs, 32);
}

class IsoTestDisc final : public psxrecomp::runtime::Disc
{
  public:
    IsoTestDisc()
    {
        buildImage();
    }

    bool readUserSector(u32 lba, std::span<u8, 2048> out) override
    {
        if (lba >= m_sectors.size())
        {
            return false;
        }

        const auto& sector = m_sectors[static_cast<size_t>(lba)];
        std::copy(sector.begin(), sector.end(), out.begin());
        return true;
    }

    u32 userSectorCount() const override
    {
        return static_cast<u32>(m_sectors.size());
    }

    const std::vector<u8>& helloData() const
    {
        return m_helloData;
    }

    const std::vector<u8>& systemCnfData() const
    {
        return m_systemCnfData;
    }

    const std::vector<u8>& nestedData() const
    {
        return m_nestedData;
    }

  private:
    std::vector<std::array<u8, kSectorSize>> m_sectors =
        std::vector<std::array<u8, kSectorSize>>(64u);
    std::vector<u8> m_helloData;
    std::vector<u8> m_systemCnfData;
    std::vector<u8> m_nestedData;

    void writeFile(u32 lba, const std::vector<u8>& data)
    {
        size_t remaining = data.size();
        size_t offset = 0;
        u32 sector = lba;
        while (remaining > 0)
        {
            auto& dst = m_sectors[static_cast<size_t>(sector)];
            const size_t chunk = std::min(remaining, dst.size());
            std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), chunk, dst.begin());
            offset += chunk;
            remaining -= chunk;
            ++sector;
        }
    }

    void buildImage()
    {
        for (auto& sector : m_sectors)
        {
            sector.fill(0);
        }

        m_helloData.resize(3000u);
        for (size_t i = 0; i < m_helloData.size(); ++i)
        {
            m_helloData[i] = static_cast<u8>((i * 7u + 3u) & 0xFFu);
        }
        m_systemCnfData.assign({'B', 'O', 'O', 'T', ' ',  '=', ' ', 'c',  'd',
                                'r', 'o', 'm', ':', '\\', 'H', 'E', 'L',  'L',
                                'O', '.', 'B', 'I', 'N',  ';', '1', '\r', '\n'});
        m_nestedData.assign(
            {'N', 'e', 's', 't', 'e', 'd', ' ', 'p', 'a', 'y', 'l', 'o', 'a', 'd', '\n'});

        writeFile(kHelloLba, m_helloData);
        writeFile(kSystemCnfLba, m_systemCnfData);
        writeFile(kNestedLba, m_nestedData);

        auto& pvd = m_sectors[kPvdSector];
        pvd[0] = 1u;
        std::memcpy(pvd.data() + 1, "CD001", 5);
        pvd[6] = 1u;
        const auto rootRecord = makeDirectoryRecord({0u}, kRootDirLba, kSectorSize, 0x02u);
        std::copy(rootRecord.begin(), rootRecord.end(), pvd.begin() + 156);

        auto& rootDir = m_sectors[kRootDirLba];
        size_t rootOffset = 0;
        appendRecord(rootDir, rootOffset,
                     makeDirectoryRecord({0u}, kRootDirLba, kSectorSize, 0x02u));
        appendRecord(rootDir, rootOffset,
                     makeDirectoryRecord({1u}, kRootDirLba, kSectorSize, 0x02u));
        appendRecord(rootDir, rootOffset,
                     makeDirectoryRecord({'H', 'E', 'L', 'L', 'O', '.', 'B', 'I', 'N', ';', '1'},
                                         kHelloLba, static_cast<u32>(m_helloData.size()), 0x00u));
        appendRecord(
            rootDir, rootOffset,
            makeDirectoryRecord({'S', 'Y', 'S', 'T', 'E', 'M', '.', 'C', 'N', 'F', ';', '1'},
                                kSystemCnfLba, static_cast<u32>(m_systemCnfData.size()), 0x00u));
        appendRecord(
            rootDir, rootOffset,
            makeDirectoryRecord({'S', 'U', 'B', 'D', 'I', 'R'}, kSubdirLba, kSectorSize, 0x02u));

        auto& subdir = m_sectors[kSubdirLba];
        size_t subdirOffset = 0;
        appendRecord(subdir, subdirOffset,
                     makeDirectoryRecord({0u}, kSubdirLba, kSectorSize, 0x02u));
        appendRecord(subdir, subdirOffset,
                     makeDirectoryRecord({1u}, kRootDirLba, kSectorSize, 0x02u));
        appendRecord(subdir, subdirOffset,
                     makeDirectoryRecord({'N', 'E', 'S', 'T', '.', 'T', 'X', 'T', ';', '1'},
                                         kNestedLba, static_cast<u32>(m_nestedData.size()), 0x00u));
    }
};

void initWithDisc(PsxSystem& system, const std::shared_ptr<psxrecomp::runtime::Disc>& disc)
{
    system.setDisc(disc);
    assert(system.initialize());
}

void testFileOpenReadSeekClose()
{
    PsxSystem system;
    auto disc = std::make_shared<IsoTestDisc>();
    initWithDisc(system, disc);

    constexpr u32 pathAddr = 0x1000;
    constexpr u32 readAddr = 0x2000;
    constexpr u32 tailAddr = 0x2400;
    u8* ram = system.getRam();
    writeCString(ram, pathAddr, "cdrom:\\hello.bin");

    u32 regs[32] = {};
    regs[4] = pathAddr;
    callB0(system, 0x32, regs);
    const int fd = static_cast<int>(regs[2]);
    assert(fd >= 2);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = static_cast<u32>(fd);
    regs[5] = readAddr;
    regs[6] = 64;
    callB0(system, 0x34, regs);
    assert(regs[2] == 64u);
    assert(std::memcmp(ram + readAddr, disc->helloData().data(), 64u) == 0);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = static_cast<u32>(fd);
    regs[5] = 2040u;
    regs[6] = 0u;
    callB0(system, 0x33, regs);
    assert(regs[2] == 2040u);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = static_cast<u32>(fd);
    regs[5] = tailAddr;
    regs[6] = 32u;
    callB0(system, 0x34, regs);
    assert(regs[2] == 32u);
    assert(std::memcmp(ram + tailAddr, disc->helloData().data() + 2040, 32u) == 0);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = static_cast<u32>(fd);
    regs[5] = static_cast<u32>(-8);
    regs[6] = 2u;
    callB0(system, 0x33, regs);
    assert(regs[2] == disc->helloData().size() - 8u);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = static_cast<u32>(fd);
    regs[5] = tailAddr;
    regs[6] = 16u;
    callB0(system, 0x34, regs);
    assert(regs[2] == 8u);
    assert(std::memcmp(ram + tailAddr, disc->helloData().data() + disc->helloData().size() - 8u,
                       8u) == 0);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = static_cast<u32>(fd);
    callB0(system, 0x36, regs);
    assert(regs[2] == static_cast<u32>(fd));

    writeCString(ram, pathAddr, "cdrom:\\missing.bin");
    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = pathAddr;
    callB0(system, 0x32, regs);
    assert(regs[2] == 0xFFFFFFFFu);

    std::cerr << "[PASS] B0 FileOpen/FileSeek/FileRead/FileClose read disc-backed data\n";
}

void testNestedOpenAndDirectoryEnumeration()
{
    PsxSystem system;
    auto disc = std::make_shared<IsoTestDisc>();
    initWithDisc(system, disc);

    constexpr u32 pathAddr = 0x1000;
    constexpr u32 readAddr = 0x2000;
    constexpr u32 dirEntryAddr = 0x3000;
    u8* ram = system.getRam();

    writeCString(ram, pathAddr, "cdrom:\\subdir\\nest.txt");
    u32 regs[32] = {};
    regs[4] = pathAddr;
    callB0(system, 0x32, regs);
    const int fd = static_cast<int>(regs[2]);
    assert(fd >= 2);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = static_cast<u32>(fd);
    regs[5] = readAddr;
    regs[6] = static_cast<u32>(disc->nestedData().size());
    callB0(system, 0x34, regs);
    assert(regs[2] == disc->nestedData().size());
    assert(std::memcmp(ram + readAddr, disc->nestedData().data(), disc->nestedData().size()) == 0);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = static_cast<u32>(fd);
    callB0(system, 0x36, regs);
    assert(regs[2] == static_cast<u32>(fd));

    writeCString(ram, pathAddr, "cdrom:\\*");
    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = pathAddr;
    regs[5] = dirEntryAddr;
    callB0(system, 0x42, regs);
    const std::string firstRootName = readDirName(ram, dirEntryAddr);
    const u32 firstRootAttr = readLe32(ram + dirEntryAddr + 20u);
    const u32 firstRootSize = readLe32(ram + dirEntryAddr + 24u);
    assert(regs[2] == dirEntryAddr);
    assert(firstRootName == "HELLO.BIN;1");
    if (firstRootAttr != 0u || firstRootSize != disc->helloData().size())
    {
        assert(false);
    }

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = dirEntryAddr;
    callB0(system, 0x43, regs);
    const std::string secondRootName = readDirName(ram, dirEntryAddr);
    const u32 secondRootAttr = readLe32(ram + dirEntryAddr + 20u);
    const u32 secondRootSize = readLe32(ram + dirEntryAddr + 24u);
    assert(regs[2] == dirEntryAddr);
    assert(secondRootName == "SYSTEM.CNF;1");
    if (secondRootAttr != 0u || secondRootSize != disc->systemCnfData().size())
    {
        assert(false);
    }

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = dirEntryAddr;
    callB0(system, 0x43, regs);
    const std::string thirdRootName = readDirName(ram, dirEntryAddr);
    const u32 thirdRootAttr = readLe32(ram + dirEntryAddr + 20u);
    const u32 thirdRootSize = readLe32(ram + dirEntryAddr + 24u);
    assert(regs[2] == dirEntryAddr);
    assert(thirdRootName == "SUBDIR");
    if (thirdRootAttr != 0x10u || thirdRootSize != kSectorSize)
    {
        assert(false);
    }

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = dirEntryAddr;
    callB0(system, 0x43, regs);
    assert(regs[2] == 0u);

    writeCString(ram, pathAddr, "cdrom:\\subdir\\*");
    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = pathAddr;
    regs[5] = dirEntryAddr;
    callB0(system, 0x42, regs);
    const std::string nestedName = readDirName(ram, dirEntryAddr);
    const u32 nestedAttr = readLe32(ram + dirEntryAddr + 20u);
    const u32 nestedSize = readLe32(ram + dirEntryAddr + 24u);
    assert(regs[2] == dirEntryAddr);
    assert(nestedName == "NEST.TXT;1");
    if (nestedAttr != 0u || nestedSize != disc->nestedData().size())
    {
        assert(false);
    }

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = dirEntryAddr;
    callB0(system, 0x43, regs);
    assert(regs[2] == 0u);

    std::cerr << "[PASS] B0 firstfile/nextfile enumerate ISO directories\n";
}

void testFileWriteToStdoutAndIsoFdRejection()
{
    PsxSystem system;
    auto disc = std::make_shared<IsoTestDisc>();
    initWithDisc(system, disc);

    constexpr u32 pathAddr = 0x1000;
    constexpr u32 writeAddr = 0x2000;
    u8* ram = system.getRam();
    writeCString(ram, pathAddr, "cdrom:\\hello.bin");
    std::memcpy(ram + writeAddr, "REV2 boot trace\n", sizeof("REV2 boot trace\n") - 1u);

    bool sawStdoutWrite = false;
    system.logger().setMinLevel(psxrecomp::runtime::LogLevel::Info);
    system.logger().setCallback(
        [&sawStdoutWrite](const psxrecomp::runtime::LogEvent& event)
        {
            if (event.level == psxrecomp::runtime::LogLevel::Info && event.category == "bios" &&
                event.message.find("REV2 boot trace") != std::string::npos)
            {
                sawStdoutWrite = true;
            }
        });

    u32 regs[32] = {};
    regs[4] = 1u;
    regs[5] = writeAddr;
    regs[6] = sizeof("REV2 boot trace\n") - 1u;
    callB0(system, 0x35, regs);
    assert(regs[2] == sizeof("REV2 boot trace\n") - 1u);
    assert(sawStdoutWrite);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = pathAddr;
    callB0(system, 0x32, regs);
    const int fd = static_cast<int>(regs[2]);
    assert(fd >= 2);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = static_cast<u32>(fd);
    regs[5] = writeAddr;
    regs[6] = 4u;
    callB0(system, 0x35, regs);
    assert(regs[2] == 0xFFFFFFFFu);

    std::fill(std::begin(regs), std::end(regs), 0u);
    regs[4] = static_cast<u32>(fd);
    callB0(system, 0x36, regs);
    assert(regs[2] == static_cast<u32>(fd));

    std::cerr << "[PASS] B0 FileWrite logs stdout and rejects ISO writes\n";
}

} // namespace

int main()
{
    testFileOpenReadSeekClose();
    testNestedOpenAndDirectoryEnumeration();
    testFileWriteToStdoutAndIsoFdRejection();
    return 0;
}
