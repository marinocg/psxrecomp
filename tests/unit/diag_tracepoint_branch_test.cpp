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
        throw std::runtime_error("failed to initialize diag tracepoint branch test system");
    }

    TracepointConfig tracepoint;
    tracepoint.name = "hot_loop_branch";
    tracepoint.pcRangeStart = 0x154720u;
    tracepoint.pcRangeEnd = 0x154720u;
    tracepoint.captureContext = true;
    tracepoint.logBranches = true;
    tracepoint.branchTakenPc = 0x154768u;
    tracepoint.branchNotTakenPc = 0x154728u;
    tracepoint.registers = {"t1", "zero", "ra"};
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
    regs[psxrecomp::Registers::T1] = 0x00000000u;
    regs[psxrecomp::Registers::RA] = 0x1547A8u;

    system.observeProgramCounter(0x154720u, regs.data(), regs.size());
    system.observeProgramCounter(0x154728u, regs.data(), regs.size());

    regs[psxrecomp::Registers::T1] = 0x00000001u;
    system.observeProgramCounter(0x154720u, regs.data(), regs.size());
    system.observeProgramCounter(0x154768u, regs.data(), regs.size());

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

    if (!contains("tracepoint=hot_loop_branch event=branch branch_pc=0x154720 next_pc=0x154728 "
                  "result=not_taken"))
    {
        throw std::runtime_error("missing not-taken branch log");
    }
    if (!contains("tracepoint=hot_loop_branch event=branch branch_pc=0x154720 next_pc=0x154768 "
                  "result=taken"))
    {
        throw std::runtime_error("missing taken branch log");
    }
    if (!contains("regs=t1=0x0,zero=0x0,ra=0x1547a8"))
    {
        throw std::runtime_error("missing operand snapshot for not-taken branch");
    }
    if (!contains("regs=t1=0x1,zero=0x0,ra=0x1547a8"))
    {
        throw std::runtime_error("missing operand snapshot for taken branch");
    }

    const std::string summary = system.diagTracepoints().formatRecentTraces();
    if (summary.find("[hot_loop_branch] branch pc=0x154720 next=0x154728 result=not_taken") ==
        std::string::npos)
    {
        throw std::runtime_error("missing not-taken branch summary");
    }
    if (summary.find("[hot_loop_branch] branch pc=0x154720 next=0x154768 result=taken") ==
        std::string::npos)
    {
        throw std::runtime_error("missing taken branch summary");
    }

    logs.clear();
    tracepoint.name = "hot_loop_branch_suppressed";
    tracepoint.repeatThreshold = 2;
    tracepoint.branchTakenPc = 0x154768u;
    tracepoint.branchNotTakenPc = 0x154728u;
    system.diagTracepoints().configure({tracepoint});
    regs[psxrecomp::Registers::T1] = 0x00000000u;
    for (int i = 0; i < 4; ++i)
    {
        system.observeProgramCounter(0x154720u, regs.data(), regs.size());
        system.observeProgramCounter(0x154728u, regs.data(), regs.size());
    }
    if (countContaining("tracepoint=hot_loop_branch_suppressed event=branch") != 3)
    {
        throw std::runtime_error("repeat suppression did not cap branch logs");
    }
    if (!contains("tracepoint=hot_loop_branch_suppressed event=repeat count=2"))
    {
        throw std::runtime_error("missing repeat log for suppressed branch case");
    }
    return 0;
}
