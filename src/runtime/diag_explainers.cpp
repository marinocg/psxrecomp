#include "psxrecomp/runtime/diag_explainers.h"

#include "psxrecomp/runtime/cdrom.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

DiagExplainerEngine::DiagExplainerEngine() = default;

void DiagExplainerEngine::configure(const std::vector<ExplainerConfig>& configs)
{
    m_configs = configs;
}

bool DiagExplainerEngine::isEnabled(ExplainerKind kind) const
{
    for (const auto& config : m_configs)
    {
        if (config.kind == kind)
        {
            return true;
        }
    }
    return false;
}

std::string DiagExplainerEngine::explainGpustat(u32 value)
{
    std::ostringstream os;
    os << "GPUSTAT=0x" << std::hex << value << std::dec;
    os << " [tex_page_x=" << (value & 0xF);
    os << " tex_page_y=" << ((value >> 4) & 1);
    os << " semi_transparency=" << ((value >> 5) & 3);
    os << " tex_depth=" << ((value >> 7) & 3);
    os << " dither=" << ((value >> 9) & 1);
    os << " draw_to_display=" << ((value >> 10) & 1);
    os << " mask_bit=" << ((value >> 11) & 1);
    os << " draw_pixels=" << ((value >> 12) & 1);
    os << " interlace_field=" << ((value >> 13) & 1);
    os << " reverse_flag=" << ((value >> 14) & 1);
    os << " tex_disable=" << ((value >> 15) & 1);
    os << " h_res_2=" << ((value >> 16) & 1);
    os << " h_res_1=" << ((value >> 17) & 3);
    os << " v_res=" << ((value >> 19) & 1);
    os << " video_mode=" << ((value >> 20) & 1);
    os << " color_depth=" << ((value >> 21) & 1);
    os << " v_interlace=" << ((value >> 22) & 1);
    os << " display_enabled=" << ((value >> 23) & 1);
    os << " irq=" << ((value >> 24) & 1);
    os << " dma_request=" << ((value >> 25) & 1);
    os << " ready_cmd=" << ((value >> 26) & 1);
    os << " ready_vram=" << ((value >> 27) & 1);
    os << " ready_dma=" << ((value >> 28) & 1);
    os << " dma_direction=" << ((value >> 29) & 3);
    os << " odd_lines=" << ((value >> 31) & 1) << "]";
    return os.str();
}

std::string DiagExplainerEngine::explainCdromIrq(u8 flags, u8 enable)
{
    static const char* irqNames[] = {"INT1/DataReady", "INT2/Complete", "INT3/Acknowledge",
                                     "INT4/EndOfRead", "INT5/Error"};
    std::ostringstream os;
    os << "CDROM_IRQ flags=0x" << std::hex << static_cast<unsigned>(flags) << " enable=0x"
       << static_cast<unsigned>(enable) << std::dec;
    const u8 activeType = flags & 0x07u;
    if (activeType >= 1 && activeType <= 5)
    {
        os << " active=" << irqNames[activeType - 1];
    }
    os << " pending=[";
    bool first = true;
    for (int i = 0; i < 5; ++i)
    {
        if ((enable >> i) & 1)
        {
            if (!first)
            {
                os << ",";
            }
            os << irqNames[i];
            first = false;
        }
    }
    os << "]";
    return os.str();
}

std::string DiagExplainerEngine::explainIrqController(u16 status, u16 mask)
{
    static const char* lineNames[] = {"VBlank", "GPU",      "CDROM", "DMA", "Timer0",  "Timer1",
                                      "Timer2", "Ctrl/Mem", "SIO",   "SPU", "Lightpen"};
    std::ostringstream os;
    os << "I_STAT=0x" << std::hex << status << " I_MASK=0x" << mask << std::dec;

    os << " pending=[";
    bool first = true;
    const u16 active = status & mask;
    for (int i = 0; i < 11; ++i)
    {
        if ((active >> i) & 1)
        {
            if (!first)
            {
                os << ",";
            }
            os << lineNames[i];
            first = false;
        }
    }
    os << "]";
    return os.str();
}

std::string DiagExplainerEngine::explainDmaChannel(u8 port, u32 control)
{
    static const char* portNames[] = {"MDECin", "MDECout", "GPU", "CDROM", "SPU", "PIO", "OTC"};
    std::ostringstream os;
    const char* portName = (port < 7) ? portNames[port] : "Unknown";
    os << "DMA" << static_cast<int>(port) << "(" << portName << ") CHCR=0x" << std::hex << control
       << std::dec;
    os << " [dir=" << (control & 1 ? "from_ram" : "to_ram");
    os << " step=" << ((control >> 1) & 1 ? "backward" : "forward");
    os << " chop=" << ((control >> 8) & 1);
    os << " sync_mode=" << ((control >> 9) & 3);
    os << " chop_dma_win=" << ((control >> 16) & 7);
    os << " chop_cpu_win=" << ((control >> 20) & 7);
    os << " start_busy=" << ((control >> 24) & 1);
    os << " start_trigger=" << ((control >> 28) & 1) << "]";
    return os.str();
}

size_t DiagExplainerEngine::explainerCount() const
{
    return m_configs.size();
}

std::string DiagExplainerEngine::explainCdromBankSummary(const DiagCdromBankTracer& tracer)
{
    return tracer.formatSummary();
}

std::string DiagExplainerEngine::explainCdromXaClassification(const Cdrom& cdrom)
{
    return cdrom.formatXaClassificationSummary();
}

std::string DiagExplainerEngine::explainCdromPostStreamValidator(const Cdrom& cdrom)
{
    return cdrom.formatPostStreamSummary();
}

std::string DiagExplainerEngine::explainCdromCpuPayloadSummary(const Cdrom& cdrom)
{
    return cdrom.formatCpuPayloadSummary();
}

std::string DiagExplainerEngine::explainCdromIrqLifecycleSummary(const Cdrom& cdrom)
{
    return cdrom.formatIrqLifecycleSummary();
}

std::string
DiagExplainerEngine::explainCdromLateBufferSummary(const DiagCdromLateBufferTracker& tracker)
{
    return tracker.formatSummary();
}

} // namespace runtime
} // namespace psxrecomp
