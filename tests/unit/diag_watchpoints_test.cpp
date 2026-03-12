#include "psxrecomp/runtime/psx_system.h"

#include <string>
#include <stdexcept>

int main()
{
    using psxrecomp::Address;
    using psxrecomp::runtime::DiagMemoryMapConfig;
    using psxrecomp::runtime::PsxSystem;
    using psxrecomp::runtime::WatchpointAction;
    using psxrecomp::runtime::WatchpointConfig;
    using psxrecomp::runtime::WatchpointKind;

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

    return 0;
}
