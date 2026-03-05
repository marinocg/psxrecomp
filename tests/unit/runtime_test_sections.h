#pragma once

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
void runRuntimeResourcePackChecks();
