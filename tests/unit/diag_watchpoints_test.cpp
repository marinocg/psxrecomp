#include "psxrecomp/runtime/psx_system.h"

#include <stdexcept>
#include <string>

int main()
{
    using psxrecomp::Address;
    using psxrecomp::runtime::DiagMemoryMapConfig;
    using psxrecomp::runtime::PsxSystem;
    using psxrecomp::runtime::WatchpointAction;
    using psxrecomp::runtime::WatchpointConfig;
    using psxrecomp::runtime::WatchpointKind;
    namespace Mmio = psxrecomp::runtime::Mmio;

    PsxSystem system;
    if (!system.initialize())
    {
        throw std::runtime_error("failed to initialize diag watchpoint test system");
    }

    WatchpointConfig readWatchpoint;
    readWatchpoint.name = "counter_read";
    readWatchpoint.kind = WatchpointKind::RamRead;
    readWatchpoint.rangeStart = 0x801665B0u;
    readWatchpoint.rangeEnd = 0x801665B3u;
    readWatchpoint.action = WatchpointAction::Summarize;

    system.diagWatchpoints().configure({readWatchpoint}, DiagMemoryMapConfig{});
    system.write<psxrecomp::u32>(0x801665B0u, 0x12345678u);
    system.diagWatchpoints().clearEvents();

    system.debugOverlay().setLastProgramCounter(0x8015D634u);
    const auto value = system.read<psxrecomp::u32>(0x801665B0u);
    if (value != 0x12345678u)
    {
        throw std::runtime_error("unexpected RAM value in read watchpoint test");
    }

    const std::string summary = system.diagWatchpoints().formatSummary();
    if (summary.find("[counter_read] read pc=0x8015d634") == std::string::npos)
    {
        throw std::runtime_error("missing read watchpoint PC in summary");
    }
    if (summary.find("addr=0x801665b0") == std::string::npos)
    {
        throw std::runtime_error("missing read watchpoint address in summary");
    }
    if (summary.find("value=0x12345678") == std::string::npos)
    {
        throw std::runtime_error("missing read watchpoint value in summary");
    }

    WatchpointConfig mmioWriteWatchpoint;
    mmioWriteWatchpoint.name = "interrupt_mask_write";
    mmioWriteWatchpoint.kind = WatchpointKind::MmioWrite;
    mmioWriteWatchpoint.rangeStart = Mmio::INTERRUPT_MASK;
    mmioWriteWatchpoint.rangeEnd = Mmio::INTERRUPT_MASK + 3u;
    mmioWriteWatchpoint.action = WatchpointAction::Summarize;

    WatchpointConfig mmioReadWatchpoint;
    mmioReadWatchpoint.name = "interrupt_mask_read";
    mmioReadWatchpoint.kind = WatchpointKind::MmioRead;
    mmioReadWatchpoint.rangeStart = Mmio::INTERRUPT_MASK;
    mmioReadWatchpoint.rangeEnd = Mmio::INTERRUPT_MASK + 3u;
    mmioReadWatchpoint.action = WatchpointAction::Summarize;

    system.diagWatchpoints().configure({mmioWriteWatchpoint, mmioReadWatchpoint},
                                       DiagMemoryMapConfig{});
    system.diagWatchpoints().clearEvents();

    system.debugOverlay().setLastProgramCounter(0x8015DAA4u);
    system.write<psxrecomp::u32>(Mmio::INTERRUPT_MASK, 0x00000009u);
    system.debugOverlay().setLastProgramCounter(0x8015DAA8u);
    const auto mask = system.read<psxrecomp::u32>(Mmio::INTERRUPT_MASK);
    if (mask != 0x00000009u)
    {
        throw std::runtime_error("unexpected MMIO value in watchpoint test");
    }

    const std::string mmioSummary = system.diagWatchpoints().formatSummary();
    if (mmioSummary.find("[interrupt_mask_write] mmio_write pc=0x8015daa4") == std::string::npos)
    {
        throw std::runtime_error("missing MMIO write watchpoint PC in summary");
    }
    if (mmioSummary.find("addr=0x1f801074") == std::string::npos)
    {
        throw std::runtime_error("missing MMIO watchpoint address in summary");
    }
    if (mmioSummary.find("value=0x9") == std::string::npos)
    {
        throw std::runtime_error("missing MMIO watchpoint value in summary");
    }
    if (mmioSummary.find("[interrupt_mask_read] mmio_read pc=0x8015daa8") == std::string::npos)
    {
        throw std::runtime_error("missing MMIO read watchpoint PC in summary");
    }

    return 0;
}
