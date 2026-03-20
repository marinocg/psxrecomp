#include "psxrecomp/runtime/psx_system.h"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

int main()
{
    using psxrecomp::u32;
    using psxrecomp::runtime::LogLevel;
    using psxrecomp::runtime::PsxSystem;
    using psxrecomp::runtime::TracepointConfig;

    PsxSystem system;
    if (!system.initialize())
    {
        throw std::runtime_error("failed to initialize diag tracepoint register test system");
    }

    TracepointConfig tracepoint;
    tracepoint.name = "hot_loop";
    tracepoint.pcRangeStart = 0x154718u;
    tracepoint.pcRangeEnd = 0x154718u;
    tracepoint.captureContext = true;
    tracepoint.repeatThreshold = 2;
    tracepoint.registers = {"s0", "s1", "t0", "t1", "ra"};
    system.diagTracepoints().configure({tracepoint});

    std::vector<std::string> logs;
    system.logger().setMinLevel(LogLevel::Info);
    system.logger().setCallback(
        [&logs](const psxrecomp::runtime::LogEvent& event)
        {
            if (event.category == "tracepoint")
            {
                logs.push_back(event.message);
            }
        });

    std::array<u32, 32> regs{};
    regs[psxrecomp::Registers::S0] = 0x80166490u;
    regs[psxrecomp::Registers::S1] = 0x00000001u;
    regs[psxrecomp::Registers::T0] = 0x80025800u;
    regs[psxrecomp::Registers::T1] = 0x00000003u;
    regs[psxrecomp::Registers::RA] = 0x1547A8u;

    for (int i = 0; i < 4; ++i)
    {
        system.observeProgramCounter(0x154718u, regs.data(), regs.size());
        system.observeProgramCounter(0x15471Cu, regs.data(), regs.size());
    }

    auto contains = [&logs](const std::string& needle)
    {
        for (const std::string& message : logs)
        {
            if (message.find(needle) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    };
    auto countContaining = [&logs](const std::string& needle)
    {
        size_t count = 0;
        for (const std::string& message : logs)
        {
            if (message.find(needle) != std::string::npos)
            {
                ++count;
            }
        }
        return count;
    };

    if (!contains("tracepoint=hot_loop event=entry pc=0x154718"))
    {
        throw std::runtime_error("missing entry tracepoint log");
    }
    if (!contains("regs=s0=0x80166490,s1=0x1,t0=0x80025800,t1=0x3,ra=0x1547a8"))
    {
        throw std::runtime_error("missing register snapshot in entry log");
    }
    if (!contains("tracepoint=hot_loop event=repeat count=2"))
    {
        throw std::runtime_error("missing repeat tracepoint log");
    }
    if (countContaining("tracepoint=hot_loop event=entry") != 3)
    {
        throw std::runtime_error("repeat suppression did not cap entry logs");
    }

    const std::string summary = system.diagTracepoints().formatRecentTraces();
    if (summary.find("[hot_loop] entry pc=0x154718") == std::string::npos)
    {
        throw std::runtime_error("missing tracepoint entry in summary");
    }
    if (summary.find("regs=s0=0x80166490,s1=0x1,t0=0x80025800,t1=0x3,ra=0x1547a8") ==
        std::string::npos)
    {
        throw std::runtime_error("missing register snapshot in summary");
    }
    return 0;
}
