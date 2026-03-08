#pragma once

namespace psxrecomp
{
namespace runtime
{
class PsxSystem;
} // namespace runtime
} // namespace psxrecomp

void runRuntimeTimerChecks(psxrecomp::runtime::PsxSystem& system);
void runRuntimeResourcePackChecks();
