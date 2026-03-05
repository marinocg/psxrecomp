#pragma once

#include <cstdlib>

namespace psxrecomp
{
namespace runtime
{

inline bool traceIrqFlowEnabled()
{
    if (const char* env = std::getenv("PSXRECOMP_TRACE_IRQ_FLOW"))
    {
        return env[0] == '1';
    }
    return false;
}

} // namespace runtime
} // namespace psxrecomp
