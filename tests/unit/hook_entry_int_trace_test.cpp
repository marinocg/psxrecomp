#include "psxrecomp/runtime/hook_entry_int_trace.h"

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    using psxrecomp::runtime::HookEntryIntTraceEngine;

    HookEntryIntTraceEngine trace;
    trace.recordInstall(0x80011000u, 0x80014000u);
    trace.beginInvocation(0x80012000u, 0x80014000u, 0x80015000u, true, 0x8001FFE0u, 0x8001FFD0u,
                          0x80011000u, 7u);
    trace.noteCommittedResume(0x80015000u);
    trace.noteReturnFromException(0x80013000u);
    trace.finishInvocation(8u);

    const std::string formatted = trace.formatRecentInvocations();
    assert(formatted.find("descriptor=0x80014000") != std::string::npos);
    assert(formatted.find("desc_resume=0x80015000") != std::string::npos);
    assert(formatted.find("committed_resume=0x80015000") != std::string::npos);
    assert(formatted.find("b017=1") != std::string::npos);
    assert(formatted.find("gen=7->8") != std::string::npos);

    std::cerr << "[PASS] HookEntryInt trace summarizes resume state\n";
    return 0;
}
