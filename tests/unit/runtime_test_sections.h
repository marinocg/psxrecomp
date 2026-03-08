#pragma once

#include "runtime_test_timer_sections.h"

namespace psxrecomp
{
namespace runtime
{
class PsxSystem;
}
} // namespace psxrecomp

void runRuntimeStateSerializationChecks(psxrecomp::runtime::PsxSystem& system);
void runRuntimeLoggingAndDumpChecks(psxrecomp::runtime::PsxSystem& system);
void runRuntimeInterruptAndTimerChecks(psxrecomp::runtime::PsxSystem& system);
