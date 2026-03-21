#include "psxrecomp/runtime/hook_entry_int_trace.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

int main()
{
    using psxrecomp::runtime::DescriptorInvalidReason;
    using psxrecomp::runtime::HookEntryIntTraceEngine;

    // Test basic trace with S0-S7 and invalid reason.
    {
        HookEntryIntTraceEngine trace;
        trace.recordInstall(0x80011000u, 0x80014000u);
        std::array<psxrecomp::u32, 8> savedS = {0x11u, 0x22u, 0x33u, 0x44u,
                                                0x55u, 0x66u, 0x77u, 0x88u};
        trace.beginInvocation(0x80012000u, 0x80014000u, 0x80015000u, true, 0x8001FFE0u, 0x8001FFD0u,
                              0x80011000u, savedS, DescriptorInvalidReason::Valid, 7u);
        trace.noteCommittedResume(0x80015000u);
        trace.noteReturnFromException(0x80013000u);
        trace.finishInvocation(8u);

        const std::string formatted = trace.formatRecentInvocations();
        assert(formatted.find("descriptor=0x80014000") != std::string::npos);
        assert(formatted.find("desc_resume=0x80015000") != std::string::npos);
        assert(formatted.find("committed_resume=0x80015000") != std::string::npos);
        assert(formatted.find("desc_valid=yes") != std::string::npos);
        assert(formatted.find("b017=1") != std::string::npos);
        assert(formatted.find("gen=7->8") != std::string::npos);

        std::cerr << "[PASS] HookEntryInt trace summarizes resume state\n";
    }

    // Test invalid descriptor classification.
    {
        HookEntryIntTraceEngine trace;
        trace.recordInstall(0x80011000u, 0x80014000u);
        std::array<psxrecomp::u32, 8> savedS{};
        trace.beginInvocation(0x80012000u, 0x80014000u, 0x0u, false, 0u, 0u, 0u, savedS,
                              DescriptorInvalidReason::Zeroed, 1u);
        trace.finishInvocation(1u);

        const std::string formatted = trace.formatRecentInvocations();
        assert(formatted.find("desc_valid=no") != std::string::npos);
        assert(formatted.find("reason=zeroed") != std::string::npos);

        std::cerr << "[PASS] HookEntryInt trace classifies zeroed descriptor\n";
    }

    // Test descriptor snapshot clobber detection.
    {
        HookEntryIntTraceEngine trace;
        trace.recordInstall(0x80011000u, 0x80014000u);

        // Simulate a descriptor in RAM.
        std::array<psxrecomp::u8, 0x200000> ram{};
        const psxrecomp::u32 offset = 0x14000u;
        // Set RA at offset 0.
        const psxrecomp::u32 ra = 0x80012340u;
        std::memcpy(ram.data() + offset, &ra, 4);
        trace.snapshotDescriptorAtInstall(ram.data(), offset, 0x200000u);

        // Before clobber — should not report clobbered.
        assert(!trace.wasDescriptorClobberedSinceInstall(ram.data(), offset, 0x200000u));

        // Clobber the RA field.
        const psxrecomp::u32 zero = 0u;
        std::memcpy(ram.data() + offset, &zero, 4);
        assert(trace.wasDescriptorClobberedSinceInstall(ram.data(), offset, 0x200000u));

        std::cerr << "[PASS] HookEntryInt trace detects descriptor clobber\n";
    }

    // Test readInstallSnapshot retrieves words from the captured snapshot.
    {
        HookEntryIntTraceEngine trace;
        trace.recordInstall(0x80011000u, 0x80014000u);

        std::array<psxrecomp::u8, 0x200000> ram{};
        const psxrecomp::u32 offset = 0x14000u;
        const psxrecomp::u32 ra = 0x8001d938u;
        const psxrecomp::u32 sp = 0x807fff78u;
        std::memcpy(ram.data() + offset + 0x00u, &ra, 4);
        std::memcpy(ram.data() + offset + 0x04u, &sp, 4);
        trace.snapshotDescriptorAtInstall(ram.data(), offset, 0x200000u);

        // Simulate game consuming the descriptor (zeroing it in RAM).
        std::memset(ram.data() + offset, 0, 0x30u);

        psxrecomp::u32 readRa = 0u;
        psxrecomp::u32 readSp = 0u;
        assert(trace.readInstallSnapshot(0x00u, readRa) && readRa == ra);
        assert(trace.readInstallSnapshot(0x04u, readSp) && readSp == sp);

        // Out-of-range read should return false.
        psxrecomp::u32 dummy = 0u;
        assert(!trace.readInstallSnapshot(0x2du, dummy)); // 0x2D + 4 > 0x30

        // No snapshot case returns false.
        HookEntryIntTraceEngine trace2;
        psxrecomp::u32 val = 0u;
        assert(!trace2.readInstallSnapshot(0x00u, val));

        std::cerr << "[PASS] HookEntryInt trace readInstallSnapshot retrieves snapshot words\n";
    }

    // Test unaligned address classification.
    {
        HookEntryIntTraceEngine trace;
        std::array<psxrecomp::u32, 8> savedS{};
        trace.beginInvocation(0x80012000u, 0x80014000u, 0x80015001u, false, 0u, 0u, 0u, savedS,
                              DescriptorInvalidReason::UnalignedAddress, 0u);
        trace.finishInvocation(0u);

        const std::string formatted = trace.formatRecentInvocations();
        assert(formatted.find("reason=unaligned") != std::string::npos);

        std::cerr << "[PASS] HookEntryInt trace classifies unaligned address\n";
    }

    std::cerr << "All HookEntryInt trace tests passed.\n";
    return 0;
}
