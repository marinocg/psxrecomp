#include "psxrecomp/runtime/callback_trace.h"

#include <array>
#include <cassert>
#include <iostream>
#include <string>

int main()
{
    using psxrecomp::runtime::CallbackTraceEngine;
    using Registers = std::array<psxrecomp::u32, 32>;

    CallbackTraceEngine engine;
    engine.beginInvocation(0x80011000u, 0x80012000u, 0x80013000u, 3u, 0x4u, 0x9u, true);
    engine.setActiveInvocationStackPointer(0x80021000u);
    engine.recordRamWrite(0x80020000u, 4u, 0u, 1u);
    engine.recordRamWrite(0x80020000u, 4u, 1u, 2u);
    engine.recordRamWrite(0x80020004u, 4u, 0x10u, 0x20u);
    engine.recordRamWrite(0x80020ff0u, 4u, 0x20u, 0x30u);
    Registers before{};
    Registers after{};
    after[2] = 1u;
    after[29] = 0x8001FFF0u;
    engine.recordCommittedRegisterDelta(before, after, 0u, 0x1234u, 0u, 0u, true);
    engine.finishInvocation(0x80014000u, true, 4u, 0x0u, 0x9u, false, nullptr, false);
    engine.beginInvocation(0x80011000u, 0x80012000u, 0x80013000u, 4u, 0x4u, 0x9u, true);
    engine.setActiveInvocationStackPointer(0x80021000u);
    engine.recordRamWrite(0x80020000u, 4u, 2u, 3u);
    engine.recordRamWrite(0x80020ffcu, 4u, 0x30u, 0x40u);
    engine.finishInvocation(0x80014000u, true, 5u, 0x0u, 0x9u, false, nullptr, false);

    const std::string summary = engine.formatRecentCallbacks();
    assert(summary.find("entry=0x80011000") != std::string::npos);
    assert(summary.find("exit=0x80014000") != std::string::npos);
    assert(summary.find("descriptor=0x80012000") != std::string::npos);
    assert(summary.find("return_site=0x80013000") != std::string::npos);
    assert(summary.find("rfe=1") != std::string::npos);
    assert(summary.find("repeat=2") != std::string::npos);
    assert(summary.find("count=2") != std::string::npos);
    assert(summary.find("total_writes=6") != std::string::npos);
    assert(summary.find("persistent_writes=4") != std::string::npos);
    assert(summary.find("stack_writes=2") != std::string::npos);
    assert(summary.find("0x80020000x3") != std::string::npos);
    assert(summary.find("0x80020ff0x1") != std::string::npos);
    assert(summary.find("v0x1") != std::string::npos);
    assert(summary.find("spx1") != std::string::npos);
    assert(summary.find("hix1") != std::string::npos);

    std::cerr << "[PASS] callback trace repeat signatures are summarized\n";
    return 0;
}
