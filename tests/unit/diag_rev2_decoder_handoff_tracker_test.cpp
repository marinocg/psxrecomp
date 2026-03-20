#include "psxrecomp/runtime/psx_system.h"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

int main()
{
    using psxrecomp::Registers::A0;
    using psxrecomp::Registers::A1;
    using psxrecomp::Registers::A2;
    using psxrecomp::runtime::LogEvent;
    using psxrecomp::runtime::LogLevel;
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    if (!system.initialize())
    {
        throw std::runtime_error("failed to initialize psx system");
    }

    auto& tracker = system.diagRev2DecoderHandoffTracker();
    tracker.setEnabled(true);

    constexpr psxrecomp::Address input = 0x80070400u;
    constexpr psxrecomp::Address output = 0x800C0000u;
    constexpr psxrecomp::Address descriptor = 0x80071000u;

    system.write<psxrecomp::u16>(descriptor + 0u, 1u);
    system.write<psxrecomp::u32>(descriptor + 8u, 0x800u);
    system.write<psxrecomp::u16>(descriptor + 16u, 0x12u);
    system.write<psxrecomp::u16>(descriptor + 18u, 0x34u);
    for (psxrecomp::u32 offset = 0; offset < 16u; ++offset)
    {
        system.write<psxrecomp::u8>(input + offset, 0u);
    }

    std::array<psxrecomp::u32, psxrecomp::Registers::NUM_REGISTERS> regs{};
    regs[A0] = input;
    regs[A1] = 3u;
    regs[A2] = descriptor;
    system.observeProgramCounter(0x15A0BCu, regs.data(), regs.size());

    std::vector<std::string> diagLogs;
    system.logger().setMinLevel(LogLevel::Info);
    system.logger().setCallback(
        [&diagLogs](const LogEvent& event)
        {
            if (event.category == "diag")
            {
                diagLogs.push_back(event.message);
            }
        });

    regs = {};
    regs[A0] = input;
    regs[A1] = output;
    regs[A2] = 0u;
    system.observeProgramCounter(0x15452Cu, regs.data(), regs.size());
    regs[A1] = output + 0x20u;
    system.observeProgramCounter(0x154758u, regs.data(), regs.size());

    const std::string zeroSummary = tracker.formatSummary();
    if (zeroSummary.find("diagnosis: populated zero-only by producer") == std::string::npos)
    {
        throw std::runtime_error("missing zero-only provenance diagnosis");
    }
    if (zeroSummary.find("segment=1 input=0x80070400 out_start=0x800c0000 out_max=0x800c0020 "
                         "produced=32") == std::string::npos)
    {
        throw std::runtime_error("missing decoder output-span accounting");
    }
    if (diagLogs.empty() ||
        diagLogs.front().find("rev2_decoder_guard diagnosis=\"populated zero-only "
                              "by producer\"") == std::string::npos)
    {
        throw std::runtime_error("missing immediate zero-payload guard log");
    }

    tracker.reset();
    system.logger().setCallback({});
    system.write<psxrecomp::u8>(input, 0x7Fu);
    system.write<psxrecomp::u8>(input, 0u);
    regs = {};
    regs[A0] = input;
    regs[A1] = 4u;
    regs[A2] = descriptor;
    system.observeProgramCounter(0x15A0BCu, regs.data(), regs.size());
    regs = {};
    regs[A0] = input;
    regs[A1] = output;
    system.observeProgramCounter(0x15452Cu, regs.data(), regs.size());

    const std::string clearedSummary = tracker.formatSummary();
    if (clearedSummary.find("diagnosis: populated nonzero then cleared") == std::string::npos)
    {
        throw std::runtime_error("missing nonzero-then-cleared diagnosis");
    }
    if (clearedSummary.find("later_cleared: yes") == std::string::npos)
    {
        throw std::runtime_error("missing cleared flag");
    }

    return 0;
}
