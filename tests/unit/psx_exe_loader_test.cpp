#include "psxrecomp/iso/psx_exe_loader.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

namespace
{

void writeLe32(std::vector<psxrecomp::u8>& buffer, size_t offset, psxrecomp::u32 value)
{
    buffer[offset] = static_cast<psxrecomp::u8>(value & 0xFF);
    buffer[offset + 1] = static_cast<psxrecomp::u8>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<psxrecomp::u8>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<psxrecomp::u8>((value >> 24) & 0xFF);
}

void writeProgramWord(std::vector<psxrecomp::u8>& buffer, size_t offset, psxrecomp::u32 value)
{
    writeLe32(buffer, psxrecomp::iso::PsxExeLoader::kHeaderSize + offset, value);
}

void writeOverlayTable(std::vector<psxrecomp::u8>& buffer, psxrecomp::u32 loadAddress,
                       psxrecomp::u32 size, psxrecomp::u32 fileOffset)
{
    const size_t tableOffset = psxrecomp::iso::kPsxExeTrailingDataOffset;
    buffer[tableOffset] = 'O';
    buffer[tableOffset + 1] = 'V';
    buffer[tableOffset + 2] = 'L';
    buffer[tableOffset + 3] = 'Y';
    writeLe32(buffer, tableOffset + 4, 1);
    writeLe32(buffer, tableOffset + 8, loadAddress);
    writeLe32(buffer, tableOffset + 12, size);
    writeLe32(buffer, tableOffset + 16, fileOffset);
}

std::vector<psxrecomp::u8> buildTestExe(psxrecomp::u32 loadSize, bool writeLoadSize)
{
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);

    for (size_t i = 0; i < psxrecomp::iso::kPsxExeReservedPrefixSize; ++i)
    {
        buffer[psxrecomp::iso::kPsxExeReservedPrefixOffset + i] =
            static_cast<psxrecomp::u8>(0xA0 + i);
    }

    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80020000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, writeLoadSize ? loadSize : 0);

    for (size_t i = 0; i < psxrecomp::iso::kPsxExeReservedGapSize; ++i)
    {
        buffer[psxrecomp::iso::kPsxExeReservedGapOffset + i] = static_cast<psxrecomp::u8>(0xB0 + i);
    }

    writeLe32(buffer, 0x28, 0x80030000);
    writeLe32(buffer, 0x2C, 0x20);
    writeLe32(buffer, 0x30, 0x80100000);
    writeLe32(buffer, 0x34, 0x1000);

    for (size_t i = 0; i < psxrecomp::iso::kPsxExeSavedRegistersSize; ++i)
    {
        buffer[psxrecomp::iso::kPsxExeSavedRegistersOffset + i] =
            static_cast<psxrecomp::u8>(0xC0 + i);
    }

    const char* title = "TEST PSX EXE";
    std::memcpy(buffer.data() + 0x4C, title, std::strlen(title));

    buffer[psxrecomp::iso::kPsxExeTrailingDataOffset] = 0x5A;
    buffer[psxrecomp::iso::kPsxExeTrailingDataOffset + 1] = 0x5B;

    for (psxrecomp::u32 i = 0; i < loadSize; ++i)
    {
        buffer[psxrecomp::iso::PsxExeLoader::kHeaderSize + i] =
            static_cast<psxrecomp::u8>(i & 0xFF);
    }

    return buffer;
}

} // namespace

int main()
{
    {
        auto buffer = buildTestExe(16, true);
        psxrecomp::iso::PsxExeImage image{};
        assert(psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
        assert(image.header.title == "TEST PSX EXE");
        assert(image.header.initialPc == 0x80010000);
        assert(image.header.initialGp == 0x80020000);
        assert(image.header.loadAddress == 0x80010000);
        assert(image.header.loadSize == 16);
        assert(image.header.bssAddress == 0x80030000);
        assert(image.header.bssSize == 0x20);
        assert(image.header.stackAddress == 0x80100000);
        assert(image.header.stackSize == 0x1000);
        assert(image.entryPoint.pc == 0x80010000);
        assert(image.entryPoint.gp == 0x80020000);
        assert(image.entryPoint.sp == 0x80100000);
        assert(image.header.reservedPrefix[0] == 0xA0);
        assert(image.header.reservedGap[0] == 0xB0);
        assert(image.header.savedRegisters[0] == 0xC0);
        assert(image.header.trailingData[0] == 0x5A);
        assert(image.programData.size() == 16);
        assert(image.programData[0] == 0x00);
        assert(image.programData[1] == 0x01);
        assert(image.programData[15] == 0x0F);
    }

    {
        auto buffer = buildTestExe(32, false);
        psxrecomp::iso::PsxExeImage image{};
        assert(psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
        assert(image.header.loadSize == 32);
        assert(image.programData.size() == 32);
    }

    {
        auto buffer = buildTestExe(16, true);
        buffer.resize(psxrecomp::iso::PsxExeLoader::kHeaderSize + 32, 0xEE);
        psxrecomp::iso::PsxExeImage image{};
        psxrecomp::iso::PsxExeDiagnostics diagnostics{};
        assert(psxrecomp::iso::PsxExeLoader::loadImage(buffer, image, &diagnostics));
        assert(!diagnostics.entries.empty());
        assert(image.programData.size() == 16);
    }

    {
        std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize, 0);
        std::memcpy(buffer.data(), "NOPE EXE", 8);
        psxrecomp::iso::PsxExeImage image{};
        psxrecomp::iso::PsxExeDiagnostics diagnostics{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image, &diagnostics));
        assert(diagnostics.hasErrors());
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x28, 0x80200000);
        writeLe32(buffer, 0x2C, 0x10);
        psxrecomp::iso::PsxExeImage image{};
        psxrecomp::iso::PsxExeDiagnostics diagnostics{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image, &diagnostics));
        assert(diagnostics.hasErrors());
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x10, 0x80200000);
        psxrecomp::iso::PsxExeImage image{};
        psxrecomp::iso::PsxExeDiagnostics diagnostics{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image, &diagnostics));
        assert(diagnostics.hasErrors());
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x14, 0x80200000);
        psxrecomp::iso::PsxExeImage image{};
        psxrecomp::iso::PsxExeDiagnostics diagnostics{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image, &diagnostics));
        assert(diagnostics.hasErrors());
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x18, 0x801FFFF0);
        writeLe32(buffer, 0x1C, 0x40);
        psxrecomp::iso::PsxExeImage image{};
        psxrecomp::iso::PsxExeDiagnostics diagnostics{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image, &diagnostics));
        assert(diagnostics.hasErrors());
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x30, 0);
        writeLe32(buffer, 0x34, 0);
        writeLe32(buffer, 0x28, 0);
        writeLe32(buffer, 0x2C, 0);
        psxrecomp::iso::PsxExeImage image{};
        assert(psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x10, 0x80010002);
        psxrecomp::iso::PsxExeImage image{};
        psxrecomp::iso::PsxExeDiagnostics diagnostics{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image, &diagnostics));
        assert(diagnostics.hasErrors());
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x30, 0x80100002);
        writeLe32(buffer, 0x34, 0x1000);
        psxrecomp::iso::PsxExeImage image{};
        psxrecomp::iso::PsxExeDiagnostics diagnostics{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image, &diagnostics));
        assert(diagnostics.hasErrors());
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x18, 0x80010002);
        psxrecomp::iso::PsxExeImage image{};
        psxrecomp::iso::PsxExeDiagnostics diagnostics{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image, &diagnostics));
        assert(diagnostics.hasErrors());
    }

    {
        auto buffer = buildTestExe(16, true);
        psxrecomp::iso::PsxExeMemoryImage memoryImage{};
        assert(psxrecomp::iso::PsxExeLoader::loadMemoryImage(buffer, memoryImage));
        assert(memoryImage.ram.size() == psxrecomp::MemoryMap::RAM_SIZE);
        assert(memoryImage.ram[0x00010000] == 0x00);
        assert(memoryImage.ram[0x0001000F] == 0x0F);
        assert(memoryImage.ram[0x00030000] == 0x00);
    }

    {
        auto buffer = buildTestExe(16, true);
        writeProgramWord(buffer, 4, 0x0000054C);
        psxrecomp::iso::PsxExeImage image{};
        assert(psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
        assert(!image.syscalls.empty());
        assert(image.syscalls[0].address == 0x80010004);
        assert(image.syscalls[0].code == 0x15);
        assert(!image.symbols.empty());
    }

    {
        auto buffer = buildTestExe(8, true);
        const psxrecomp::u32 overlayOffset = psxrecomp::iso::PsxExeLoader::kHeaderSize + 8;
        buffer.resize(static_cast<size_t>(overlayOffset + 8), 0);
        buffer[overlayOffset] = 0xAA;
        buffer[overlayOffset + 7] = 0xBB;
        writeOverlayTable(buffer, 0x80020000, 8, overlayOffset);

        psxrecomp::iso::PsxExeImage image{};
        assert(psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
        assert(image.segments.size() == 2);
        assert(image.segments[1].loadAddress == 0x80020000);
        assert(image.segments[1].data[0] == 0xAA);
        assert(image.segments[1].data[7] == 0xBB);

        psxrecomp::iso::PsxExeMemoryImage memoryImage{};
        assert(psxrecomp::iso::PsxExeLoader::loadMemoryImage(buffer, memoryImage));
        assert(memoryImage.ram[0x00020000] == 0xAA);
        assert(memoryImage.ram[0x00020007] == 0xBB);
    }

    {
        struct FixtureGuard
        {
            std::filesystem::path path;
            ~FixtureGuard()
            {
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        };

        auto tempDir = std::filesystem::temp_directory_path();
        auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::random_device randomDevice;
        std::uniform_int_distribution<int> dist(0, 0xFFFF);
        auto suffix = std::to_string(timestamp) + "_" + std::to_string(dist(randomDevice));
        auto fixturePath =
            std::filesystem::absolute(tempDir / ("psx_exe_fixture_" + suffix + ".psx"));
        FixtureGuard guard{fixturePath};

        auto buffer = buildTestExe(32, true);
        std::ofstream outFile(fixturePath, std::ios::binary);
        outFile.write(reinterpret_cast<const char*>(buffer.data()),
                      static_cast<std::streamsize>(buffer.size()));
        assert(static_cast<bool>(outFile));
        outFile.close();
        assert(std::filesystem::exists(fixturePath));
        assert(std::filesystem::file_size(fixturePath) == buffer.size());
        std::ifstream verifyFile(fixturePath, std::ios::binary | std::ios::ate);
        assert(static_cast<bool>(verifyFile));
        verifyFile.close();
        std::ifstream verifyStringFile(fixturePath.string(), std::ios::binary | std::ios::ate);
        assert(static_cast<bool>(verifyStringFile));
        verifyStringFile.close();

        psxrecomp::iso::PsxExeImage image{};
        assert(psxrecomp::iso::PsxExeLoader::loadFromFile(fixturePath.string(), image));
        assert(image.programData.size() == 32);
        assert(image.entryPoint.pc == 0x80010000);
    }

    return 0;
}
