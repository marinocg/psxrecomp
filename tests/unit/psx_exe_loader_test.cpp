#include "psxrecomp/iso/psx_exe_loader.h"

#include <cassert>
#include <cstring>
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

std::vector<psxrecomp::u8> buildTestExe(psxrecomp::u32 loadSize, bool writeLoadSize)
{
    std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize + loadSize, 0);
    std::memcpy(buffer.data(), "PS-X EXE", 8);

    writeLe32(buffer, 0x10, 0x80010000);
    writeLe32(buffer, 0x14, 0x80020000);
    writeLe32(buffer, 0x18, 0x80010000);
    writeLe32(buffer, 0x1C, writeLoadSize ? loadSize : 0);
    writeLe32(buffer, 0x28, 0x80030000);
    writeLe32(buffer, 0x2C, 0x20);
    writeLe32(buffer, 0x30, 0x80100000);
    writeLe32(buffer, 0x34, 0x1000);

    const char* title = "TEST PSX EXE";
    std::memcpy(buffer.data() + 0x4C, title, std::strlen(title));

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
        std::vector<psxrecomp::u8> buffer(psxrecomp::iso::PsxExeLoader::kHeaderSize, 0);
        std::memcpy(buffer.data(), "NOPE EXE", 8);
        psxrecomp::iso::PsxExeImage image{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x28, 0x80200000);
        writeLe32(buffer, 0x2C, 0x10);
        psxrecomp::iso::PsxExeImage image{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x10, 0x80200000);
        psxrecomp::iso::PsxExeImage image{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x14, 0x80200000);
        psxrecomp::iso::PsxExeImage image{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x18, 0x801FFFF0);
        writeLe32(buffer, 0x1C, 0x40);
        psxrecomp::iso::PsxExeImage image{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
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
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
    }

    {
        auto buffer = buildTestExe(16, true);
        writeLe32(buffer, 0x30, 0x80100002);
        writeLe32(buffer, 0x34, 0x1000);
        psxrecomp::iso::PsxExeImage image{};
        assert(!psxrecomp::iso::PsxExeLoader::loadImage(buffer, image));
    }

    return 0;
}
