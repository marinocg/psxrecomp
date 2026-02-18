#include "psxrecomp/runtime/debug_overlay.h"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{
namespace
{
constexpr double CpuClockHz = 33868800.0;

uint8_t glyphRow(char ch, int row)
{
    if (row < 0 || row > 6)
    {
        return 0;
    }
    if (ch >= 'a' && ch <= 'z')
    {
        ch = static_cast<char>('A' + (ch - 'a'));
    }
    switch (ch)
    {
    case 'A':
        return (row == 0) ? 0x0E : (row == 3) ? 0x1F : (row >= 1 && row <= 6) ? 0x11 : 0x00;
    case 'B':
        return (row == 0 || row == 3 || row == 6) ? 0x1E : 0x11;
    case 'C':
        return (row == 0 || row == 6) ? 0x0E : (row >= 1 && row <= 5) ? 0x11 : 0x00;
    case 'D':
        return (row == 0 || row == 6) ? 0x1E : 0x11;
    case 'E':
        return (row == 0 || row == 3 || row == 6) ? 0x1F : 0x10;
    case 'F':
        return (row == 0 || row == 3) ? 0x1F : (row == 6) ? 0x10 : 0x10;
    case 'I':
        return (row == 0 || row == 6) ? 0x1F : 0x04;
    case 'M':
        return (row == 0) ? 0x11 : (row == 1) ? 0x1B : (row == 2) ? 0x15 : 0x11;
    case 'P':
        return (row == 0 || row == 3) ? 0x1E : (row < 3) ? 0x11 : 0x10;
    case 'Q':
        return (row == 0) ? 0x0E : (row == 5) ? 0x12 : (row == 6) ? 0x0D : 0x11;
    case 'R':
        return (row == 0 || row == 3) ? 0x1E : (row < 3) ? 0x11 : (row == 4) ? 0x12 : 0x11;
    case 'S':
        return (row == 0 || row == 3 || row == 6) ? 0x0F : (row < 3) ? 0x10 : 0x01;
    case 'X':
        return (row == 0 || row == 6)   ? 0x11
               : (row == 1 || row == 5) ? 0x0A
               : (row == 2 || row == 4) ? 0x04
                                        : 0x00;
    case 'Y':
        return (row < 3) ? ((row == 0) ? 0x11 : (row == 1) ? 0x0A : 0x04) : 0x04;
    case '=':
        return (row == 2 || row == 4) ? 0x1F : 0x00;
    case ':':
        return (row == 2 || row == 5) ? 0x04 : 0x00;
    case '.':
        return (row == 6) ? 0x04 : 0x00;
    case '0':
        return (row == 0 || row == 6) ? 0x0E : 0x11;
    case '1':
        return (row == 0) ? 0x04 : (row == 1) ? 0x0C : (row == 6) ? 0x0E : 0x04;
    case '2':
        return (row == 0 || row == 3 || row == 6) ? 0x1E : (row < 3) ? 0x01 : 0x10;
    case '3':
        return (row == 0 || row == 3 || row == 6) ? 0x1E : 0x01;
    case '4':
        return (row == 3) ? 0x1F : (row < 3) ? 0x11 : 0x01;
    case '5':
        return (row == 0 || row == 3 || row == 6) ? 0x1E : (row < 3) ? 0x10 : 0x01;
    case '6':
        return (row == 0 || row == 3 || row == 6) ? 0x0E : (row < 3) ? 0x10 : 0x11;
    case '7':
        return (row == 0) ? 0x1F : (row == 1) ? 0x01 : (row == 2) ? 0x02 : (row == 3) ? 0x04 : 0x08;
    case '8':
        return (row == 0 || row == 3 || row == 6) ? 0x0E : 0x11;
    case '9':
        return (row == 0 || row == 3 || row == 6) ? 0x0E : (row < 3) ? 0x11 : 0x01;
    default:
        return 0x00;
    }
}
} // namespace

void RuntimeDebugOverlay::reset()
{
    m_frameCounter = 0;
    m_lastFrameCycles = 0;
    m_dmaTransfers = 0;
    m_interruptsRaised = 0;
    m_lastProgramCounter = 0;
}

void RuntimeDebugOverlay::setLastFrameCycles(uint64_t cycles)
{
    m_lastFrameCycles = cycles;
    ++m_frameCounter;
}

void RuntimeDebugOverlay::incrementDmaTransfers()
{
    ++m_dmaTransfers;
}

void RuntimeDebugOverlay::incrementInterruptsRaised()
{
    ++m_interruptsRaised;
}

uint64_t RuntimeDebugOverlay::dmaTransfers() const
{
    return m_dmaTransfers;
}

uint64_t RuntimeDebugOverlay::interruptsRaised() const
{
    return m_interruptsRaised;
}

uint64_t RuntimeDebugOverlay::frameCounter() const
{
    return m_frameCounter;
}

uint64_t RuntimeDebugOverlay::lastFrameCycles() const
{
    return m_lastFrameCycles;
}

uint32_t RuntimeDebugOverlay::lastProgramCounter() const
{
    return m_lastProgramCounter;
}

void RuntimeDebugOverlay::setLastProgramCounter(uint32_t pc)
{
    m_lastProgramCounter = pc;
}

std::string RuntimeDebugOverlay::renderText() const
{
    std::ostringstream stream;
    const double fps =
        (m_lastFrameCycles == 0) ? 0.0 : (CpuClockHz / static_cast<double>(m_lastFrameCycles));
    stream << "frame=" << m_frameCounter << " fps=" << std::fixed << std::setprecision(2) << fps
           << " cycles=" << m_lastFrameCycles << " dma=" << m_dmaTransfers
           << " irq=" << m_interruptsRaised << " pc=0x" << std::hex << std::uppercase
           << m_lastProgramCounter;
    return stream.str();
}

void RuntimeDebugOverlay::drawOnFrameBuffer(std::vector<uint16_t>& framebuffer, size_t width,
                                            size_t height) const
{
    if (framebuffer.size() < width * height)
    {
        return;
    }

    constexpr size_t glyphWidth = 5;
    constexpr size_t glyphHeight = 7;
    constexpr size_t glyphAdvance = 6;
    const std::string text = renderText();
    const size_t startX = 4;
    const size_t startY = 4;

    const auto putPixel = [&](size_t x, size_t y, uint16_t color)
    {
        if (x >= width || y >= height)
        {
            return;
        }
        framebuffer[y * width + x] = color;
    };

    // Draw a solid black strip behind text for readability.
    const size_t boxWidth = std::min(width, startX + text.size() * glyphAdvance + 2);
    const size_t boxHeight = std::min(height, startY + glyphHeight + 2);
    for (size_t y = startY; y < boxHeight; ++y)
    {
        for (size_t x = startX; x < boxWidth; ++x)
        {
            putPixel(x, y, 0x0000u);
        }
    }

    for (size_t i = 0; i < text.size(); ++i)
    {
        const size_t glyphX = startX + i * glyphAdvance;
        for (size_t row = 0; row < glyphHeight; ++row)
        {
            const uint8_t bits = glyphRow(text[i], static_cast<int>(row));
            for (size_t col = 0; col < glyphWidth; ++col)
            {
                if ((bits & (1u << (glyphWidth - 1 - col))) == 0)
                {
                    continue;
                }
                putPixel(glyphX + col, startY + row, 0x7FFFu);
            }
        }
    }
}

} // namespace runtime
} // namespace psxrecomp
